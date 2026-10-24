import { Play } from 'lucide-react-native';
import { Pressable, StyleSheet, Text, View } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

import { GlassPanel } from '../components/GlassPanel';
import { colors, fonts } from '../theme';

export function HomeScreen({ onPlay }: { onPlay: () => void }) {
  return (
    <SafeAreaView style={styles.screen}>
      <View style={styles.hero}>
        <Text style={styles.title}>Project 64</Text>
        <Text style={styles.subtitle}>Autonomous chessboard</Text>
      </View>
      <GlassPanel>
        <Pressable
          onPress={onPlay}
          accessibilityRole="button"
          accessibilityLabel="Play game"
          style={({ pressed }) => [styles.play, pressed && { opacity: 0.7 }]}
        >
          <Play size={20} color={colors.text} />
          <Text style={styles.playText}>Play Game</Text>
        </Pressable>
      </GlassPanel>
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  screen: { flex: 1, backgroundColor: colors.background, paddingHorizontal: 16, justifyContent: 'center', gap: 32 },
  hero: { alignItems: 'center' },
  title: { color: colors.text, fontFamily: fonts.mono, fontSize: 40, fontWeight: '700' },
  subtitle: { color: colors.textMuted, fontFamily: fonts.sans, fontSize: 15, marginTop: 6 },
  play: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'center',
    gap: 10,
    minHeight: 52,
    borderRadius: 12,
    borderWidth: 1,
    borderColor: colors.glowConnected,
    backgroundColor: colors.highlightLegal,
  },
  playText: { color: colors.text, fontFamily: fonts.sans, fontSize: 17, fontWeight: '700' },
});
