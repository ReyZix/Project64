#include "LichessSession.h"

#include <algorithm>
#include <iostream>
#include <sstream>

namespace {
const int RECONNECT_MS = 5000;
const int SEEK_GRACE_MS = 3000;
void log(const std::string& message) { std::cout << "[Lichess] " << message << "\n"; }
} // namespace

SyncPlan planSync(const std::vector<std::string>& local, const std::vector<std::string>& remote) {
  if (remote == local) return {SyncPlan::Nothing, ""};
  bool oneNewMove = remote.size() == local.size() + 1 && std::equal(local.begin(), local.end(), remote.begin());
  if (oneNewMove) return {SyncPlan::Apply, remote.back()};
  return {SyncPlan::Rebuild, ""};
}

bool isLiveStatus(const std::string& status) { return status == "created" || status == "started"; }

LichessSession::LichessSession(EventQueue& queue, Game& game, Callbacks callbacks)
    : queue_(queue), game_(game), callbacks_(std::move(callbacks)) {}

LichessSession::~LichessSession() {
  // Close every stream before the rest of the session goes away.
  eventStream_.reset();
  searchStream_.reset();
  gameStream_.reset();
}

// --- sign in ---------------------------------------------------------------------------------

bool LichessSession::signIn(const std::string& token, const std::string& baseUrl) {
  auto candidate = std::make_unique<LichessClient>(token, baseUrl);
  try {
    Json me = candidate->account();
    userId_ = me["id"].str();
    username_ = me["username"].str(userId_);
  } catch (const LichessError& e) {
    say(e.what());
    return false;
  }
  client_ = std::move(candidate);
  phase_ = Phase::Lobby;
  log("signed in as " + username_);
  listenForEvents();
  return true;
}

// --- games starting and challenges -----------------------------------------------------------

void LichessSession::listenForEvents() {
  int streamNo = ++eventStreamNo_;
  eventStream_ = client_->streamEvents(
      [this, streamNo](const Json& event) {
        queue_.post([this, streamNo, event] {
          if (streamNo == eventStreamNo_) onEvent(event);
        });
      },
      [this, streamNo](const std::string& error) {
        queue_.post([this, streamNo, error] {
          if (streamNo != eventStreamNo_) return;
          if (!error.empty()) say(error);
          queue_.post([this, streamNo] { if (streamNo == eventStreamNo_) listenForEvents(); }, RECONNECT_MS);
        });
      });
}

void LichessSession::onEvent(const Json& event) {
  std::string type = event["type"].str();
  const Json& c = event["challenge"];
  auto forget = [this](const std::string& id) {
    incoming_.erase(std::remove_if(incoming_.begin(), incoming_.end(), [&](const Challenge& x) { return x.id == id; }),
                    incoming_.end());
  };

  if (type == "gameStart") {
    const Json& info = event["game"];
    std::string status = info["status"]["name"].str();
    if (status.empty() || isLiveStatus(status)) {
      openGame(info["gameId"].str(info["id"].str()), info["color"].str(), info["opponent"]["username"].str());
    }
  } else if (type == "challenge") {
    if (c["challenger"]["id"].str() == userId_) return; // our own challenge
    std::string id = c["id"].str();
    forget(id);
    std::string tc = c["timeControl"]["show"].str("no clock");
    incoming_.push_back({id, c["challenger"]["name"].str("?"), tc});
    say("Challenge from " + incoming_.back().from + " (" + tc + "). Type: accept " + id + "   or   decline " + id);
  } else if (type == "challengeCanceled") {
    forget(c["id"].str());
  } else if (type == "challengeDeclined") {
    forget(c["id"].str());
    if (!outgoingChallengeId_.empty() && c["id"].str() == outgoingChallengeId_) {
      searchNo_++;
      searchStream_.reset();
      outgoingChallengeId_.clear();
      phase_ = Phase::Lobby;
      say(c["destUser"]["name"].str("Your friend") + " declined the challenge.");
    }
  }
}

// --- lobby actions ---------------------------------------------------------------------------

void LichessSession::findOpponent(TimeControl tc) {
  if (!client_ || phase_ != Phase::Lobby) return;
  phase_ = Phase::Seeking;
  int searchNo = ++searchNo_;
  searchStream_ = client_->seek(tc, [this, searchNo](const std::string& error) {
    // When an opponent is found Lichess closes this stream just before it sends gameStart,
    // so wait a moment before deciding that nobody was found.
    queue_.post([this, searchNo, error] {
      if (searchNo != searchNo_ || phase_ != Phase::Seeking) return; // a game started or we cancelled
      phase_ = Phase::Lobby;
      say(error.empty() ? "No opponent found. Try again." : error);
    }, error.empty() ? SEEK_GRACE_MS : 0);
  });
}

void LichessSession::challengeFriend(const std::string& username, TimeControl tc) {
  if (!client_ || username.empty() || phase_ != Phase::Lobby) return;
  phase_ = Phase::Challenging;
  int searchNo = ++searchNo_;
  searchStream_ = client_->challenge(
      username, tc,
      [this, searchNo, username](const Json& line) {
        std::string id = line["id"].str(line["challenge"]["id"].str());
        if (id.empty()) return;
        queue_.post([this, searchNo, id, username] {
          if (searchNo != searchNo_) return;
          outgoingChallengeId_ = id;
          log("challenge " + id + " sent to " + username);
        });
      },
      [this, searchNo, username](const std::string& error) {
        queue_.post([this, searchNo, error, username] {
          if (searchNo != searchNo_ || phase_ != Phase::Challenging) return;
          outgoingChallengeId_.clear();
          phase_ = Phase::Lobby;
          say(error.empty() ? username + " did not accept the challenge." : error);
        }, error.empty() ? SEEK_GRACE_MS : 0); // same short wait as findOpponent: gameStart may be on its way
      });
}

void LichessSession::cancelSearch() {
  std::string id = outgoingChallengeId_;
  searchNo_++;
  searchStream_.reset();
  outgoingChallengeId_.clear();
  if (!id.empty() && client_) {
    try {
      client_->cancelChallenge(id);
    } catch (const LichessError&) {
      // it may already be gone; nothing to do
    }
  }
  if (phase_ == Phase::Seeking || phase_ == Phase::Challenging) phase_ = Phase::Lobby;
}

void LichessSession::answerChallenge(const std::string& id, bool accept) {
  incoming_.erase(std::remove_if(incoming_.begin(), incoming_.end(), [&](const Challenge& x) { return x.id == id; }),
                  incoming_.end());
  if (!client_) return;
  try {
    // Accepting starts the game; the event stream then sends gameStart.
    if (accept) client_->acceptChallenge(id);
    else client_->declineChallenge(id);
  } catch (const LichessError& e) {
    say(e.what());
  }
}

// --- the live game ---------------------------------------------------------------------------

void LichessSession::openGame(const std::string& gameId, const std::string& color, const std::string& opponent) {
  if (!client_ || gameId.empty() || gameId == gameId_) return;
  searchNo_++;
  searchStream_.reset();
  outgoingChallengeId_.clear();
  incoming_.clear();

  gameId_ = gameId;
  playingWhite_ = color != "black";
  opponent_ = opponent.empty() ? "Opponent" : opponent;
  confirmedMoves_.clear();
  game_.reset();
  phase_ = Phase::Playing;
  log("game " + gameId + " started, you play " + (playingWhite_ ? "white" : "black"));
  connectGameStream();
}

void LichessSession::connectGameStream() {
  int streamNo = ++gameStreamNo_;
  gameStream_ = client_->streamGame(
      gameId_,
      [this, streamNo](const Json& event) {
        queue_.post([this, streamNo, event] {
          if (streamNo == gameStreamNo_) onGameEvent(event);
        });
      },
      [this, streamNo](const std::string& error) {
        queue_.post([this, streamNo, error] {
          if (streamNo != gameStreamNo_ || phase_ != Phase::Playing) return;
          if (!error.empty()) say(error);
          // The stream dropped while the game is still on: reopen it. gameFull then resyncs everything.
          log("game stream closed early, reconnecting");
          queue_.post([this, streamNo] {
            if (streamNo == gameStreamNo_ && phase_ == Phase::Playing) connectGameStream();
          }, RECONNECT_MS);
        });
      });
}

void LichessSession::onGameEvent(const Json& event) {
  std::string type = event["type"].str();
  if (type == "gameFull") {
    // The first message of a game stream: who plays which color, and the starting position.
    if (event["black"]["id"].str() == userId_) playingWhite_ = false;
    else if (event["white"]["id"].str() == userId_) playingWhite_ = true;
    const Json& them = playingWhite_ ? event["black"] : event["white"];
    opponent_ = them["name"].str(opponent_);

    std::string fen = event["initialFen"].str("startpos");
    if (fen == "startpos") fen = Game::START_FEN;
    if (fen != game_.startFen()) game_.reset(fen);
    syncWithLichess(event["state"]);
  } else if (type == "gameState") {
    syncWithLichess(event);
  } else if (type == "opponentGone" && event["gone"].truthy()) {
    say(opponent_ + " left the game.");
  }
}

void LichessSession::syncWithLichess(const Json& state) {
  // Lichess sends all moves so far as one string: "e2e4 e7e5 g1f3".
  std::vector<std::string> remote;
  Game replay;
  replay.reset(game_.startFen());
  std::istringstream words(state["moves"].str());
  for (std::string move; words >> move;) {
    move = replay.normalizeCastle(move);
    replay.play(move);
    remote.push_back(move);
  }
  confirmedMoves_ = remote;

  SyncPlan plan = planSync(game_.moves(), remote);
  if (plan.kind == SyncPlan::Apply) {
    log("move received from Lichess: " + plan.uci);
    if (callbacks_.onOpponentMove) callbacks_.onOpponentMove(plan.uci);
  } else if (plan.kind == SyncPlan::Rebuild) {
    log("board reloaded from Lichess (" + std::to_string(remote.size()) + " moves)");
    game_.reset(game_.startFen(), remote);
    if (callbacks_.onRebuilt) callbacks_.onRebuilt();
  }

  std::string status = state["status"].str("started");
  if (!isLiveStatus(status) && phase_ == Phase::Playing) {
    std::string winner = state["winner"].str();
    phase_ = Phase::Finished;
    gameStreamNo_++; // the game is over, so its stream is done too
    if (winner.empty()) say("Game over: " + status + ".");
    else say(std::string((winner == "white") == playingWhite_ ? "You won" : "You lost") + " (" + status + ").");
  }
}

bool LichessSession::sendMove(const std::string& uci) {
  if (!client_ || gameId_.empty()) return false;
  try {
    client_->makeMove(gameId_, uci);
    return true;
  } catch (const LichessError& e) {
    say(e.code == LichessError::Rejected ? "Lichess refused " + uci + ": " + e.what() : e.what());
    return false;
  }
}

void LichessSession::resign() {
  if (!client_ || gameId_.empty() || phase_ != Phase::Playing) return;
  try {
    if (confirmedMoves_.size() < 2) client_->abortGame(gameId_);
    else client_->resign(gameId_);
  } catch (const LichessError& e) {
    say(e.what());
  }
}

void LichessSession::leaveGame() {
  gameStreamNo_++;
  gameStream_.reset();
  gameId_.clear();
  confirmedMoves_.clear();
  game_.reset();
  if (client_) phase_ = Phase::Lobby;
}
