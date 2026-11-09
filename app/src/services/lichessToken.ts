import { Platform } from 'react-native';
import * as SecureStore from 'expo-secure-store';

/*
 * The user's Lichess personal access token (scope: board:play, challenge:read, challenge:write).
 * On the phone it lives in the iOS Keychain / Android Keystore through expo-secure-store.
 * The web build has no secure store, so there it is kept in memory only and asked for again on reload.
 * It is never written into the code or the repository.
 */

const KEY = 'lichess-token';
let memoryToken: string | null = null;

export async function loadLichessToken(): Promise<string | null> {
  if (Platform.OS === 'web') return memoryToken;
  return SecureStore.getItemAsync(KEY);
}

export async function saveLichessToken(token: string): Promise<void> {
  if (Platform.OS === 'web') {
    memoryToken = token;
    return;
  }
  await SecureStore.setItemAsync(KEY, token);
}

export async function clearLichessToken(): Promise<void> {
  memoryToken = null;
  if (Platform.OS !== 'web') await SecureStore.deleteItemAsync(KEY);
}
