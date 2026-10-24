import { BlurView } from 'expo-blur';
import { StyleSheet, View, type ViewProps } from 'react-native';

import { colors } from '../theme';

interface GlassPanelProps extends ViewProps {
  /** Ambient green border glow, e.g. while hardware is connected. */
  glow?: boolean;
}

/**
 * Frosted glass container for every floating HUD element.
 * The outer view carries the glow (a shadow would be clipped by the inner overflow:hidden).
 * Android renders the translucent fill without real blur unless a BlurTargetView is wired up.
 */
export function GlassPanel({ glow = false, style, children, ...rest }: GlassPanelProps) {
  return (
    <View style={[styles.shadow, glow && styles.glow, style]} {...rest}>
      <View style={[styles.clip, glow && { borderColor: colors.glowConnected }]}>
        <BlurView intensity={40} tint="systemThinMaterialDark" style={StyleSheet.absoluteFill} />
        <View style={[StyleSheet.absoluteFill, styles.fill]} />
        <View style={styles.content}>{children}</View>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  shadow: { borderRadius: 18 },
  glow: { boxShadow: `0 0 14px ${colors.glowConnected}` },
  clip: {
    borderRadius: 18,
    borderWidth: 1,
    borderColor: colors.glassBorder,
    overflow: 'hidden',
  },
  fill: { backgroundColor: colors.glassFill },
  content: { padding: 14 },
});
