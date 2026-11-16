#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "EventQueue.h"
#include "Game.h"
#include "LichessClient.h"

/*
 * One signed-in Lichess player: the lobby (find an opponent, challenges) and the live game.
 *
 *   your move       -> Lichess checks it -> then it is played here and sent to the board
 *   opponent's move -> arrives on the game stream -> onOpponentMove -> board first, then the screen
 *
 * Lichess is the referee: after every move it sends the full move list, and we compare it with ours.
 */

/** What to do after comparing our move list with the one Lichess sent. */
struct SyncPlan {
  enum Kind { Nothing, Apply, Rebuild };
  Kind kind = Nothing;
  std::string uci; // the new move, when kind is Apply
};

/** Same list: nothing. Exactly one new move: apply it. Anything else: reload the game from Lichess. */
SyncPlan planSync(const std::vector<std::string>& local, const std::vector<std::string>& remote);

/** True while a game is still being played ("created" or "started"). */
bool isLiveStatus(const std::string& status);

class LichessSession {
 public:
  enum class Phase { SignedOut, Lobby, Seeking, Challenging, Playing, Finished };

  struct Challenge {
    std::string id, from, timeControl;
  };

  struct Callbacks {
    /** The opponent moved. The caller sends it to the board first and plays it only if that worked. */
    std::function<void(const std::string& uci)> onOpponentMove;
    /** The whole game was reloaded from Lichess, so redraw it. */
    std::function<void()> onRebuilt;
    /** A message for the player. */
    std::function<void(const std::string& text)> say;
  };

  LichessSession(EventQueue& queue, Game& game, Callbacks callbacks);
  ~LichessSession();

  /** Checks the token with Lichess and starts listening for games and challenges. */
  bool signIn(const std::string& token, const std::string& baseUrl);

  // lobby
  void findOpponent(TimeControl tc);
  void challengeFriend(const std::string& username, TimeControl tc);
  void cancelSearch();
  void answerChallenge(const std::string& id, bool accept);
  const std::vector<Challenge>& incoming() const { return incoming_; }

  // in a game
  /** Sends our move to Lichess. True once Lichess accepted it; the caller then plays it. */
  bool sendMove(const std::string& uci);
  /** Resigns, or aborts if fewer than two moves were played (Lichess only allows abort that early). */
  void resign();
  void leaveGame();

  Phase phase() const { return phase_; }
  bool playingWhite() const { return playingWhite_; }
  bool myTurn() const { return phase_ == Phase::Playing && game_.whiteToMove() == playingWhite_; }
  const std::string& username() const { return username_; }
  const std::string& opponent() const { return opponent_; }

 private:
  void listenForEvents();
  void onEvent(const Json& event);
  void openGame(const std::string& gameId, const std::string& color, const std::string& opponent);
  void connectGameStream();
  void onGameEvent(const Json& event);
  void syncWithLichess(const Json& state);
  void say(const std::string& text) { if (callbacks_.say) callbacks_.say(text); }

  EventQueue& queue_;
  Game& game_;
  Callbacks callbacks_;
  Phase phase_ = Phase::SignedOut;

  std::unique_ptr<LichessClient> client_;
  std::string userId_, username_;

  // Each stream gets a number, so a late message from an old stream can be recognised and ignored.
  std::unique_ptr<LichessStream> eventStream_, searchStream_, gameStream_;
  int eventStreamNo_ = 0, searchNo_ = 0, gameStreamNo_ = 0;

  std::vector<Challenge> incoming_;
  std::string outgoingChallengeId_;

  std::string gameId_, opponent_;
  bool playingWhite_ = true;
  std::vector<std::string> confirmedMoves_; // the last move list Lichess sent
};
