import { useState } from 'react';
import { Pressable, StyleSheet, Text, TextInput, View } from 'react-native';

import type { useLichess } from '../game/useLichess';
import type { TimeControl } from '../services/LichessService';
import { colors, fonts } from '../theme';

/*
 * Online lobby and in-game controls for Lichess: sign in with a personal token, find a random opponent,
 * challenge a friend, answer incoming challenges, resign/abort, and go back to the lobby.
 */

// Board API seeks only allow Rapid and slower; these work for both seeks and friend challenges.
const TIME_CONTROLS: TimeControl[] = [
  { minutes: 10, increment: 0 },
  { minutes: 10, increment: 5 },
  { minutes: 15, increment: 10 },
  { minutes: 30, increment: 0 },
];

type Lichess = ReturnType<typeof useLichess>;

/** onInputFocus lets the screen scroll up so the keyboard does not cover the box being typed in. */
export function LichessPanel({ lichess, onInputFocus }: { lichess: Lichess; onInputFocus?: () => void }) {
  const [token, setToken] = useState('');
  const [friend, setFriend] = useState('');
  const [tcIndex, setTcIndex] = useState(0);
  const tc = TIME_CONTROLS[tcIndex];
  const { phase, account, error, notice, incoming, game } = lichess;

  return (
    <View style={styles.panel}>
      {phase === 'SIGNED_OUT' || phase === 'CHECKING' ? (
        <>
          <Text style={styles.label}>
            Lichess personal token (lichess.org → Preferences → API access tokens, with "Play games using the Board
            API" and "Send, accept and reject challenges")
          </Text>
          <TextInput
            style={styles.input}
            value={token}
            onChangeText={setToken}
            onFocus={onInputFocus}
            placeholder="lip_..."
            placeholderTextColor={colors.textMuted}
            autoCapitalize="none"
            autoCorrect={false}
            secureTextEntry
            accessibilityLabel="Lichess token"
          />
          <Button
            label={phase === 'CHECKING' ? 'Checking…' : 'Sign in to Lichess'}
            disabled={phase === 'CHECKING' || !token.trim()}
            onPress={() => {
              void lichess.signIn(token);
              setToken('');
            }}
          />
        </>
      ) : null}

      {phase === 'LOBBY' && (
        <>
          <View style={styles.row}>
            <Text style={styles.text}>Signed in as {account?.username}</Text>
            <Button label="Sign out" small onPress={() => void lichess.signOut()} />
          </View>
          <Text style={styles.label}>Time control (minutes + seconds per move)</Text>
          <View style={styles.row}>
            {TIME_CONTROLS.map((t, i) => (
              <Button
                key={`${t.minutes}+${t.increment}`}
                label={`${t.minutes}+${t.increment}`}
                selected={i === tcIndex}
                onPress={() => setTcIndex(i)}
              />
            ))}
          </View>
          <Button label="Find random opponent" onPress={() => lichess.findOpponent(tc)} />
          <View style={styles.row}>
            <TextInput
              style={[styles.input, styles.grow]}
              value={friend}
              onChangeText={setFriend}
              onFocus={onInputFocus}
              placeholder="Friend's Lichess username"
              placeholderTextColor={colors.textMuted}
              autoCapitalize="none"
              autoCorrect={false}
              accessibilityLabel="Friend's Lichess username"
            />
            <Button label="Challenge" disabled={!friend.trim()} onPress={() => lichess.challengeFriend(friend, tc)} />
          </View>
          {incoming.map((c) => (
            <View key={c.id} style={styles.row}>
              <Text style={[styles.text, styles.grow]}>
                {c.from}
                {c.rating ? ` (${c.rating})` : ''} challenges you · {c.timeControl}
              </Text>
              <Button label="Accept" small onPress={() => lichess.answerChallenge(c.id, true)} />
              <Button label="Decline" small onPress={() => lichess.answerChallenge(c.id, false)} />
            </View>
          ))}
        </>
      )}

      {(phase === 'SEEKING' || phase === 'CHALLENGING') && (
        <View style={styles.row}>
          <Text style={[styles.text, styles.grow]}>
            {phase === 'SEEKING' ? `Looking for an opponent (${tc.minutes}+${tc.increment})…` : 'Waiting for your friend to accept…'}
          </Text>
          <Button label="Cancel" small onPress={lichess.cancelSearch} />
        </View>
      )}

      {phase === 'PLAYING' && (
        <View style={styles.row}>
          <Text style={[styles.text, styles.grow]}>
            {game?.opponentGone ? 'Your opponent left the game.' : `Playing ${game?.opponent ?? 'opponent'} on Lichess`}
          </Text>
          <Button label="Resign" small danger onPress={lichess.resign} />
        </View>
      )}

      {phase === 'FINISHED' && (
        <View style={styles.row}>
          <Text style={[styles.text, styles.grow]}>Game finished on Lichess</Text>
          <Button label="Back to lobby" small onPress={lichess.leaveGame} />
        </View>
      )}

      {notice && <Text style={styles.text}>{notice}</Text>}
      {error && (
        <Pressable onPress={lichess.clearError} accessibilityRole="button" accessibilityLabel="Dismiss Lichess error">
          <Text style={[styles.text, { color: colors.danger }]}>{error}</Text>
        </Pressable>
      )}
    </View>
  );
}

function Button({
  label,
  onPress,
  disabled,
  selected,
  small,
  danger,
}: {
  label: string;
  onPress: () => void;
  disabled?: boolean;
  selected?: boolean;
  small?: boolean;
  danger?: boolean;
}) {
  return (
    <Pressable
      onPress={onPress}
      disabled={disabled}
      accessibilityRole="button"
      accessibilityLabel={label}
      accessibilityState={{ disabled, selected }}
      style={({ pressed }) => [
        styles.btn,
        !small && styles.grow,
        selected && styles.btnSelected,
        danger && styles.btnDanger,
        (pressed || disabled) && { opacity: disabled ? 0.4 : 0.7 },
      ]}
    >
      <Text style={styles.btnText}>{label}</Text>
    </Pressable>
  );
}

const styles = StyleSheet.create({
  panel: { gap: 8, marginTop: 12 },
  row: { flexDirection: 'row', alignItems: 'center', gap: 6 },
  grow: { flex: 1 },
  label: { color: colors.textMuted, fontFamily: fonts.sans, fontSize: 12 },
  text: { color: colors.text, fontFamily: fonts.sans, fontSize: 13 },
  input: {
    minHeight: 40,
    paddingHorizontal: 12,
    borderRadius: 10,
    borderWidth: 1,
    borderColor: colors.glassBorder,
    color: colors.text,
    fontFamily: fonts.mono,
    backgroundColor: 'rgba(255, 255, 255, 0.08)',
  },
  btn: {
    minHeight: 40,
    paddingHorizontal: 12,
    alignItems: 'center',
    justifyContent: 'center',
    borderRadius: 10,
    borderWidth: 1,
    borderColor: colors.glassBorder,
    backgroundColor: 'rgba(255, 255, 255, 0.08)',
  },
  btnSelected: { backgroundColor: 'rgba(76, 175, 80, 0.3)', borderColor: colors.glowConnected },
  btnDanger: { backgroundColor: colors.danger, borderColor: 'rgba(255, 255, 255, 0.25)' },
  btnText: { color: colors.text, fontFamily: fonts.sans, fontSize: 13, fontWeight: '600' },
});
