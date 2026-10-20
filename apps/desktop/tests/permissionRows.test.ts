import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import { isLinux } from '../src/lib/platform.ts';

const permissionRow = readFileSync(
  new URL('../src/lib/components/PermissionRow.svelte', import.meta.url),
  'utf8'
);
const onboarding = readFileSync(
  new URL('../src/lib/components/Onboarding.svelte', import.meta.url),
  'utf8'
);
const onboardingRoute = readFileSync(
  new URL('../src/routes/onboarding/+page.svelte', import.meta.url),
  'utf8'
);
const launchRoute = readFileSync(new URL('../src/routes/+page.svelte', import.meta.url), 'utf8');
const mainRoute = readFileSync(new URL('../src/routes/main/+page.svelte', import.meta.url), 'utf8');
const permissionsClient = readFileSync(
  new URL('../src/lib/data/permissions.ts', import.meta.url),
  'utf8'
);

test('denied permission rows stay calm and compact', () => {
  assert.doesNotMatch(permissionRow, /Turn this on in System Settings/);
  assert.doesNotMatch(permissionRow, /class:danger|\\.danger/);
  assert.match(permissionRow, /class:attention/);
  assert.match(permissionRow, /Open System Settings/);
});

test('onboarding permission flow keeps camera optional', () => {
  assert.equal((onboarding.match(/<PermissionRow/g) ?? []).length, 4);
  assert.doesNotMatch(onboarding, /Relaunch now/);
  assert.match(onboarding, /requiredReadyCount === 3/);
  assert.match(onboarding, /3 required ready/);
  assert.doesNotMatch(onboarding, /readyCount === 4/);

  const cameraRow = onboarding.slice(
    onboarding.indexOf('title="Camera"'),
    onboarding.indexOf('title="Accessibility"')
  );
  assert.doesNotMatch(cameraRow, /\srequired(?:\s|$)/);
  assert.match(cameraRow, /you can always join with it off/);

  const accessibilityGate = onboarding.slice(
    onboarding.indexOf('const accessibilityRowStatus'),
    onboarding.indexOf('const shellClass')
  );
  assert.doesNotMatch(accessibilityGate, /cameraStatus/);
});

test('required permission gates exclude camera', () => {
  assert.match(onboardingRoute, /const requiredPermissionsReady/);
  assert.doesNotMatch(
    onboardingRoute.slice(
      onboardingRoute.indexOf('const requiredPermissionsReady'),
      onboardingRoute.indexOf('/** Map the Rust mic/camera')
    ),
    /cameraStatus/
  );
  assert.doesNotMatch(launchRoute, /checkCamera/);
  assert.doesNotMatch(mainRoute, /checkCamera/);
});

test('Linux can complete identity onboarding and stay on the main menu without unavailable permissions', () => {
  assert.equal(isLinux('Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/605.1.15'), true);
  assert.equal(isLinux('Mozilla/5.0 (Windows NT 10.0; Win64; x64)'), false);
  assert.equal(isLinux('Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)'), false);
  assert.equal(isLinux('Mozilla/5.0 (Linux; Android 14)'), false);
  assert.match(launchRoute, /permissionsOk: isLinux\(\) \|\| \(await permissionsOk\(\)\)/);
  assert.match(mainRoute, /if \(!hasTauri \|\| isLinux\(\)\) return;/);
  assert.match(onboardingRoute, /if \(isLinux\(\)\) \{\s*\/\/[^\n]*\n\s*step = 'identity';/);
  assert.match(onboardingRoute, /const requiredPermissionsReady = \$derived\(\s*isLinux\(\) \|\|/);
  assert.match(onboardingRoute, /if \(!name\.trim\(\)\) return;\s*completeOnboarding/);
});

test('Linux niri uses XWayland for WebKitGTK only when X11 is available and backend was not chosen', () => {
  const lib = readFileSync(new URL('../src-tauri/src/lib.rs', import.meta.url), 'utf8');
  const linuxStartup = lib.slice(lib.indexOf('#[cfg(not(target_os = "macos"))]\n#[cfg_attr(mobile, tauri::mobile_entry_point)]'));
  assert.match(linuxStartup, /#\[cfg\(target_os = "linux"\)\]\s*if std::env::var_os\("GDK_BACKEND"\)\.is_none\(\)/);
  assert.match(linuxStartup, /std::env::var_os\("WAYLAND_DISPLAY"\)\.is_some\(\)/);
  assert.match(linuxStartup, /std::env::var_os\("DISPLAY"\)\.is_some\(\)/);
  assert.match(linuxStartup, /name\.eq_ignore_ascii_case\("niri"\)/);
  assert.match(linuxStartup, /std::env::set_var\("GDK_BACKEND", "x11"\)/);
});

test('privacy settings opener uses native command before plugin fallback', () => {
  assert.match(permissionsClient, /COMMANDS\.openPrivacySettings/);
  assert.match(permissionsClient, /openUrl\(SETTINGS_URLS\[which\]\)/);
  assert.match(permissionsClient, /native opener returned false, falling back/);
});

test('stale Accessibility grants get a safe guided repair and relaunch', () => {
  assert.match(permissionRow, /Remove the stale Petal row/);
  assert.match(permissionRow, /Add <code>\/Applications\/Petal\.app<\/code>, then enable it/);
  assert.match(permissionRow, /Petal could not restart\. Quit Petal/);
  assert.match(permissionRow, /\{:else if status === 'repair'\}/);

  const accessibilityHandler = onboardingRoute.slice(
    onboardingRoute.indexOf('async function handleAccessibilityAction()'),
    onboardingRoute.indexOf('async function handleRecheckPermissions()')
  );
  assert.match(accessibilityHandler, /accessibilityStatus === 'repair'/);
  assert.match(accessibilityHandler, /openPrivacySettings\('accessibility'\)/);
  assert.match(onboardingRoute, /async function handleAccessibilityRepairRestart\(\)/);
  assert.match(onboardingRoute, /restartApp\('accessibility-stale-grant-repair'\)/);
  assert.match(accessibilityHandler, /applyAccessibilityRepair\(\{ type: 'settings-opened' \}\)/);
  assert.match(onboardingRoute, /async function handleAccessibilityRecheck\(\)/);
  assert.match(onboardingRoute, /type: 'explicit-recheck', trusted: await checkAccessibility\(\)/);
  assert.doesNotMatch(accessibilityHandler, /tccutil|reset Accessibility/);
  assert.match(onboarding, /Open Accessibility Settings/);
  assert.match(permissionRow, /Recheck Accessibility/);
});
