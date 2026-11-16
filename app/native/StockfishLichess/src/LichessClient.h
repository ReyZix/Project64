#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include "Json.h"

/*
 * Talks to the Lichess Board API (https://lichess.org/api#tag/board) for online games.
 *
 * There are two kinds of calls:
 *   - short requests: sign in, send a move, accept a challenge. One at a time.
 *   - streams: a connection that stays open while Lichess sends one line of JSON per event
 *     (a game starting, the opponent's move). Each stream reads on its own thread.
 *
 * Lichess asks apps to wait a minute after an HTTP 429 ("too many requests"), so we do.
 */

struct TimeControl {
  int minutes = 10;
  int increment = 0;
};

struct LichessError : std::runtime_error {
  enum Code { BadToken, RateLimited, Rejected, Network };
  Code code;
  LichessError(Code c, const std::string& message) : std::runtime_error(message), code(c) {}
};

/** An open stream. close() (or deleting it) ends the stream and waits for its thread. */
class LichessStream {
 public:
  ~LichessStream() { close(); }
  void close() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
  }

 private:
  friend class LichessClient;
  std::atomic<bool> stop_{false};
  std::thread thread_;
};

class LichessClient {
 public:
  using OnLine = std::function<void(const Json& event)>;
  /** Called once when a stream ends. `error` is empty if it ended normally or was closed by us. */
  using OnDone = std::function<void(const std::string& error)>;

  LichessClient(const std::string& token, const std::string& baseUrl = "https://lichess.org");

  // --- short requests; they throw LichessError when something goes wrong ---
  Json account(); // checks the token and returns { id, username }
  void makeMove(const std::string& gameId, const std::string& uci);
  void resign(const std::string& gameId);
  void abortGame(const std::string& gameId);
  void acceptChallenge(const std::string& id);
  void declineChallenge(const std::string& id);
  void cancelChallenge(const std::string& id);

  // --- streams; callbacks run on the stream's own thread ---
  /** gameStart, gameFinish, challenge, challengeCanceled, challengeDeclined. */
  std::unique_ptr<LichessStream> streamEvents(OnLine onLine, OnDone onDone);
  /** gameFull first, then gameState after every move. */
  std::unique_ptr<LichessStream> streamGame(const std::string& gameId, OnLine onLine, OnDone onDone);
  /** Looks for a random opponent. The search lasts only while this stream is open. */
  std::unique_ptr<LichessStream> seek(TimeControl tc, OnDone onDone);
  /** Challenges one user. The first line is the challenge itself (it has the id). */
  std::unique_ptr<LichessStream> challenge(const std::string& username, TimeControl tc, OnLine onLine, OnDone onDone);

 private:
  Json request(const std::string& method, const std::string& path, const std::string& body = "");
  std::unique_ptr<LichessStream> stream(const std::string& method, const std::string& path, const std::string& body,
                                        OnLine onLine, OnDone onDone, bool expectKeepAlive = false);
  /** Seconds left before we may call Lichess again; 0 if we are not being rate limited. */
  int rateLimitSecondsLeft() const;
  /** Turns a failed HTTP status into the right LichessError. */
  LichessError failure(long status, const std::string& body);

  std::string token_;
  std::string baseUrl_;
  std::mutex requestMutex_; // one short request at a time
  std::atomic<long long> rateLimitedUntilMs_{0};
};
