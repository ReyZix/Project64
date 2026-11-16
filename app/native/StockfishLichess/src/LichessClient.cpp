#include "LichessClient.h"

#include <curl/curl.h>
#include <iostream>

namespace {

const int RATE_LIMIT_MS = 60 * 1000;
// Lichess sends an empty keep-alive line every few seconds on its event and game streams.
// If nothing at all arrives for this long, the connection is dead even if nobody closed it.
const long SILENCE_LIMIT_SECONDS = 30;

long long nowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void log(const std::string& message) { std::cout << "[Lichess] " << message << "\n"; }

// libcurl hands us the reply in pieces; this adds each piece to a string.
size_t collectBody(char* data, size_t size, size_t count, void* user) {
  static_cast<std::string*>(user)->append(data, size * count);
  return size * count;
}

// Everything a running stream needs, shared with libcurl's callbacks.
struct StreamState {
  CURL* curl = nullptr;
  std::atomic<bool>* stop = nullptr;
  LichessClient::OnLine onLine;
  std::string pending;   // text received that does not end in a newline yet
  std::string errorBody; // the reply body when Lichess answered with an error
};

void emitLine(StreamState& state, const std::string& line) {
  if (line.find_first_not_of(" \r\t") == std::string::npos) return; // empty keep-alive line
  Json event;
  if (Json::parse(line, event) && state.onLine) state.onLine(event); // skip a line we cannot read
}

size_t streamBody(char* data, size_t size, size_t count, void* user) {
  auto& state = *static_cast<StreamState*>(user);
  long status = 0;
  curl_easy_getinfo(state.curl, CURLINFO_RESPONSE_CODE, &status);
  if (status >= 400) {
    state.errorBody.append(data, size * count);
    return size * count;
  }
  state.pending.append(data, size * count);
  size_t newline;
  while ((newline = state.pending.find('\n')) != std::string::npos) {
    std::string line = state.pending.substr(0, newline);
    state.pending.erase(0, newline + 1);
    emitLine(state, line);
  }
  return size * count;
}

// libcurl calls this regularly; returning 1 cancels the transfer. That is how close() works.
int stopRequested(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  return static_cast<StreamState*>(user)->stop->load() ? 1 : 0;
}

struct CurlHandle {
  CURL* curl = curl_easy_init();
  curl_slist* headers = nullptr;
  ~CurlHandle() {
    curl_slist_free_all(headers);
    if (curl) curl_easy_cleanup(curl);
  }
};

void prepare(CurlHandle& h, const std::string& token, const std::string& method, const std::string& url,
             const std::string& body) {
  h.headers = curl_slist_append(h.headers, ("Authorization: Bearer " + token).c_str());
  h.headers = curl_slist_append(h.headers, "Accept: application/x-ndjson, application/json");
  curl_easy_setopt(h.curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(h.curl, CURLOPT_HTTPHEADER, h.headers);
  curl_easy_setopt(h.curl, CURLOPT_CONNECTTIMEOUT, 15L);
  curl_easy_setopt(h.curl, CURLOPT_NOSIGNAL, 1L);
  if (method == "POST") {
    curl_easy_setopt(h.curl, CURLOPT_POST, 1L);
    curl_easy_setopt(h.curl, CURLOPT_COPYPOSTFIELDS, body.c_str()); // form fields, e.g. time=10&increment=0
  }
}

std::string urlEncode(const std::string& text) {
  char* encoded = curl_easy_escape(nullptr, text.c_str(), static_cast<int>(text.size()));
  std::string out = encoded ? encoded : "";
  curl_free(encoded);
  return out;
}

} // namespace

LichessClient::LichessClient(const std::string& token, const std::string& baseUrl) : token_(token), baseUrl_(baseUrl) {
  static const bool ready = (curl_global_init(CURL_GLOBAL_DEFAULT), true);
  (void)ready;
  while (!baseUrl_.empty() && baseUrl_.back() == '/') baseUrl_.pop_back();
}

int LichessClient::rateLimitSecondsLeft() const {
  long long left = rateLimitedUntilMs_.load() - nowMs();
  return left > 0 ? static_cast<int>((left + 999) / 1000) : 0;
}

LichessError LichessClient::failure(long status, const std::string& body) {
  if (status == 401) return LichessError(LichessError::BadToken, "Lichess rejected the token. Check it and sign in again.");
  if (status == 429) {
    rateLimitedUntilMs_ = nowMs() + RATE_LIMIT_MS;
    return LichessError(LichessError::RateLimited, "Lichess asked us to slow down. Try again in a minute.");
  }
  // Lichess explains a refusal as {"error":"..."}.
  Json parsed;
  std::string reason = Json::parse(body, parsed) ? parsed["error"].str() : "";
  if (reason.empty()) reason = "Lichess refused the request (" + std::to_string(status) + ").";
  return LichessError(LichessError::Rejected, reason);
}

Json LichessClient::request(const std::string& method, const std::string& path, const std::string& body) {
  std::lock_guard<std::mutex> oneAtATime(requestMutex_);
  if (int wait = rateLimitSecondsLeft()) {
    throw LichessError(LichessError::RateLimited,
                       "Lichess asked us to slow down. Try again in " + std::to_string(wait) + "s.");
  }
  CurlHandle h;
  std::string reply;
  prepare(h, token_, method, baseUrl_ + path, body);
  curl_easy_setopt(h.curl, CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(h.curl, CURLOPT_WRITEFUNCTION, collectBody);
  curl_easy_setopt(h.curl, CURLOPT_WRITEDATA, &reply);

  CURLcode result = curl_easy_perform(h.curl);
  if (result != CURLE_OK) {
    throw LichessError(LichessError::Network, std::string("Could not reach Lichess: ") + curl_easy_strerror(result));
  }
  long status = 0;
  curl_easy_getinfo(h.curl, CURLINFO_RESPONSE_CODE, &status);
  if (status >= 400) throw failure(status, reply);

  Json parsed;
  Json::parse(reply, parsed);
  return parsed;
}

Json LichessClient::account() { return request("GET", "/api/account"); }

void LichessClient::makeMove(const std::string& gameId, const std::string& uci) {
  log("move sent to Lichess: " + uci);
  request("POST", "/api/board/game/" + gameId + "/move/" + uci);
}

void LichessClient::resign(const std::string& gameId) { request("POST", "/api/board/game/" + gameId + "/resign"); }
void LichessClient::abortGame(const std::string& gameId) { request("POST", "/api/board/game/" + gameId + "/abort"); }
void LichessClient::acceptChallenge(const std::string& id) { request("POST", "/api/challenge/" + id + "/accept"); }
void LichessClient::declineChallenge(const std::string& id) { request("POST", "/api/challenge/" + id + "/decline"); }
void LichessClient::cancelChallenge(const std::string& id) { request("POST", "/api/challenge/" + id + "/cancel"); }

std::unique_ptr<LichessStream> LichessClient::stream(const std::string& method, const std::string& path,
                                                     const std::string& body, OnLine onLine, OnDone onDone,
                                                     bool expectKeepAlive) {
  auto handle = std::make_unique<LichessStream>();
  LichessStream* raw = handle.get();
  std::string url = baseUrl_ + path;

  raw->thread_ = std::thread([this, raw, method, url, body, onLine, onDone, expectKeepAlive] {
    if (int wait = rateLimitSecondsLeft()) {
      onDone("Lichess asked us to slow down. Try again in " + std::to_string(wait) + "s.");
      return;
    }
    CurlHandle h;
    StreamState state;
    state.curl = h.curl;
    state.stop = &raw->stop_;
    state.onLine = onLine;
    prepare(h, token_, method, url, body);
    curl_easy_setopt(h.curl, CURLOPT_WRITEFUNCTION, streamBody);
    curl_easy_setopt(h.curl, CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(h.curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(h.curl, CURLOPT_XFERINFOFUNCTION, stopRequested);
    curl_easy_setopt(h.curl, CURLOPT_XFERINFODATA, &state);
    if (expectKeepAlive) {
      // Give up when less than 1 byte per second arrives for SILENCE_LIMIT_SECONDS.
      curl_easy_setopt(h.curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
      curl_easy_setopt(h.curl, CURLOPT_LOW_SPEED_TIME, SILENCE_LIMIT_SECONDS);
    }

    CURLcode result = curl_easy_perform(h.curl); // returns when Lichess or close() ends the stream
    long status = 0;
    curl_easy_getinfo(h.curl, CURLINFO_RESPONSE_CODE, &status);

    if (raw->stop_) return onDone("");                       // we closed it
    if (status >= 400) return onDone(failure(status, state.errorBody).what());
    if (result != CURLE_OK) {
      return onDone(status == 0 ? std::string("Could not reach Lichess: ") + curl_easy_strerror(result)
                                : "The connection to Lichess dropped.");
    }
    emitLine(state, state.pending); // a last line with no newline after it
    onDone("");
  });
  return handle;
}

std::unique_ptr<LichessStream> LichessClient::streamEvents(OnLine onLine, OnDone onDone) {
  return stream("GET", "/api/stream/event", "", onLine, onDone, true);
}

std::unique_ptr<LichessStream> LichessClient::streamGame(const std::string& gameId, OnLine onLine, OnDone onDone) {
  return stream("GET", "/api/board/game/stream/" + gameId, "", onLine, onDone, true);
}

std::unique_ptr<LichessStream> LichessClient::seek(TimeControl tc, OnDone onDone) {
  log("seeking a random opponent (" + std::to_string(tc.minutes) + "+" + std::to_string(tc.increment) + ", casual)");
  std::string body = "rated=false&time=" + std::to_string(tc.minutes) + "&increment=" + std::to_string(tc.increment);
  return stream("POST", "/api/board/seek", body, nullptr, onDone);
}

std::unique_ptr<LichessStream> LichessClient::challenge(const std::string& username, TimeControl tc, OnLine onLine,
                                                        OnDone onDone) {
  log("challenging " + username + " (" + std::to_string(tc.minutes) + "+" + std::to_string(tc.increment) + ")");
  // keepAliveStream keeps the challenge open while we wait; otherwise Lichess expires it after 20 seconds.
  std::string body = "rated=false&clock.limit=" + std::to_string(tc.minutes * 60) +
                     "&clock.increment=" + std::to_string(tc.increment) + "&keepAliveStream=true";
  return stream("POST", "/api/challenge/" + urlEncode(username), body, onLine, onDone);
}
