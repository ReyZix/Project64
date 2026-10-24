import { StatusBar } from 'expo-status-bar';
import { useState } from 'react';
import { SafeAreaProvider } from 'react-native-safe-area-context';

import { GameScreen } from './src/screens/GameScreen';
import { HomeScreen } from './src/screens/HomeScreen';

export default function App() {
  // shortcut: two screens switched by state; move to expo-router when more screens appear
  const [screen, setScreen] = useState<'home' | 'game'>('home');
  return (
    <SafeAreaProvider>
      {screen === 'home' ? (
        <HomeScreen onPlay={() => setScreen('game')} />
      ) : (
        <GameScreen onExit={() => setScreen('home')} />
      )}
      <StatusBar style="light" />
    </SafeAreaProvider>
  );
}
