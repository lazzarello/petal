//! Compile-only Linux session. No room connection, capture, or remote control.
// shortcut: Linux has no session backend; replace these stubs when implementing Linux media.
#![allow(unused_variables)]

use std::sync::{
    atomic::{AtomicBool, AtomicU64, Ordering},
    Arc,
};

use crate::camera_session::ActiveCamera;
use crate::platform::cg::WindowFrame;
use crate::remote_control_core::{RemoteControlMode, RemoteControlPolicy};
pub(crate) use crate::room_generation::RoomGeneration;
use crate::screen_audio::ShareAudioState;
use crate::transport::publisher::RoomConnection;

const UNSUPPORTED: &str = "Meeting sessions and native media are not supported on Linux yet";
pub(crate) use crate::meeting_core::RoomJoinError as SessionError;

#[derive(Debug, Clone, thiserror::Error, serde::Serialize)]
#[serde(tag = "kind", content = "message", rename_all = "camelCase")]
pub enum ShareError {
    #[error("capture failed: {0}")]
    Capture(String),
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub(crate) enum SharedWindowScreenStatus {
    NotShared,
    OnScreen(WindowFrame),
    OffScreen,
    Closed,
}

#[derive(Default)]
pub struct SessionState {
    mic_control_lock: tokio::sync::Mutex<()>,
    camera_control_lock: tokio::sync::Mutex<()>,
    room_generation: Arc<AtomicU64>,
    desired_camera_on: AtomicBool,
    camera_heal_active: AtomicBool,
}

impl SessionState {
    pub(crate) fn begin_room_generation(&self) -> RoomGeneration {
        let value = self.room_generation.fetch_add(1, Ordering::SeqCst) + 1;
        RoomGeneration::new(self.room_generation.clone(), value)
    }

    pub(crate) fn current_room_generation(&self) -> RoomGeneration {
        RoomGeneration::new(
            self.room_generation.clone(),
            self.room_generation.load(Ordering::SeqCst),
        )
    }

    pub(crate) fn invalidate_room_generation(&self) {
        self.room_generation.fetch_add(1, Ordering::SeqCst);
    }

    pub(crate) fn is_in_room(&self) -> bool {
        false
    }
    pub(crate) fn current_room_record(&self) -> Option<crate::rooms::RoomRecord> {
        None
    }
    pub(crate) fn control_channel_snapshot(&self) -> Option<(Arc<RoomConnection>, String)> {
        None
    }
    pub(crate) fn remote_control_allowed(&self) -> bool {
        false
    }
    pub(crate) fn remote_control_policy(&self) -> RemoteControlPolicy {
        RemoteControlPolicy::Off
    }
    pub(crate) fn share_allows_remote_control(&self, window_id: u32) -> bool {
        false
    }
    pub(crate) fn control_mode_for(&self, window_id: u32) -> RemoteControlMode {
        RemoteControlMode::Unknown
    }
    pub(crate) fn active_share_frame(&self, window_id: u32) -> Option<WindowFrame> {
        None
    }
    pub(crate) fn active_share_source_title(&self, window_id: u32) -> Option<String> {
        None
    }
    pub(crate) fn active_share_is_display(&self, window_id: u32) -> bool {
        false
    }
    pub(crate) fn active_share_pid(&self, window_id: u32) -> Option<i32> {
        None
    }
    pub(crate) fn shared_window_screen_status(&self, window_id: u32) -> SharedWindowScreenStatus {
        SharedWindowScreenStatus::NotShared
    }
    pub(crate) fn shared_window_ids(&self) -> Vec<u32> {
        Vec::new()
    }
    pub(crate) fn is_share_active(&self, window_id: u32) -> bool {
        false
    }
    pub(crate) fn set_ai_chat_ducking(&self, duck: bool) {}
    pub(crate) fn mic_muted(&self) -> bool {
        true
    }
    pub(crate) fn set_mic_muted(&self, muted: bool) -> Result<bool, String> {
        Err(UNSUPPORTED.into())
    }
    pub(crate) fn camera_publishing(&self) -> bool {
        false
    }
    pub(crate) fn camera_intent(&self) -> bool {
        self.desired_camera_on.load(Ordering::SeqCst)
    }
    pub(crate) fn set_camera_intent(&self, enabled: bool) {
        self.desired_camera_on.store(enabled, Ordering::SeqCst);
    }
    pub(crate) fn try_begin_camera_heal(&self) -> bool {
        self.camera_heal_active
            .compare_exchange(false, true, Ordering::SeqCst, Ordering::SeqCst)
            .is_ok()
    }
    pub(crate) fn end_camera_heal(&self) {
        self.camera_heal_active.store(false, Ordering::SeqCst);
    }
    pub(crate) fn take_active_camera(&self) -> Option<ActiveCamera> {
        None
    }
    pub(crate) fn take_active_camera_matching(
        &self,
        status: &crate::transport::camera::CameraStatus,
    ) -> Option<ActiveCamera> {
        None
    }
    pub(crate) fn put_active_camera(&self, camera: &mut Option<ActiveCamera>) -> bool {
        // Never consume a capture: the caller retains ownership for teardown.
        camera.is_none()
    }
    pub(crate) async fn lock_mic_control(&self) -> tokio::sync::MutexGuard<'_, ()> {
        self.mic_control_lock.lock().await
    }
    pub(crate) async fn lock_camera_control(&self) -> tokio::sync::MutexGuard<'_, ()> {
        self.camera_control_lock.lock().await
    }
    pub(crate) async fn set_audio_devices(
        &self,
        recording_id: Option<String>,
        playout_id: Option<String>,
        preferences: &crate::transport::audio::AudioDevicePreferences,
    ) -> crate::transport::audio::AppliedAudioDevices {
        crate::transport::audio::AppliedAudioDevices {
            mic_error: recording_id.map(|_| UNSUPPORTED.into()),
            speaker_error: playout_id.map(|_| UNSUPPORTED.into()),
            ..Default::default()
        }
    }
}

#[tauri::command]
pub async fn join_room_command(
    app: tauri::AppHandle,
    rooms: tauri::State<'_, crate::rooms::RoomsState>,
    state: tauri::State<'_, SessionState>,
    room_name: String,
    identity: String,
    display_name: String,
    remote_control_allowed: bool,
    remote_control_policy: Option<RemoteControlPolicy>,
    identity_palette_index: Option<u8>,
) -> Result<crate::rooms::RoomRecord, SessionError> {
    Err(SessionError::Config(UNSUPPORTED.into()))
}

#[tauri::command]
pub async fn leave_room_command(
    app: tauri::AppHandle,
    state: tauri::State<'_, SessionState>,
) -> Result<(), String> {
    Err(UNSUPPORTED.into())
}

pub async fn leave_room(app: &tauri::AppHandle, state: &SessionState) {
    state.invalidate_room_generation();
    state.set_camera_intent(false);
}

#[tauri::command]
pub fn current_room(state: tauri::State<'_, SessionState>) -> Option<String> {
    None
}

#[tauri::command]
pub fn room_presence(
    state: tauri::State<'_, SessionState>,
) -> Vec<crate::presence::PresentParticipant> {
    Vec::new()
}

#[tauri::command]
pub async fn set_display_name(
    app: tauri::AppHandle,
    state: tauri::State<'_, SessionState>,
    name: String,
) -> Result<(), String> {
    Err(UNSUPPORTED.into())
}

#[tauri::command]
pub fn remote_control_allowed(state: tauri::State<'_, SessionState>) -> bool {
    false
}

#[tauri::command]
pub fn remote_control_policy(state: tauri::State<'_, SessionState>) -> RemoteControlPolicy {
    RemoteControlPolicy::Off
}

#[tauri::command]
pub fn set_remote_control_policy(
    app: tauri::AppHandle,
    state: tauri::State<'_, SessionState>,
    policy: RemoteControlPolicy,
) -> Result<RemoteControlPolicy, String> {
    Err(UNSUPPORTED.into())
}

#[tauri::command]
pub fn set_remote_control_allowed(
    app: tauri::AppHandle,
    state: tauri::State<'_, SessionState>,
    allowed: bool,
) -> Result<bool, String> {
    Err(UNSUPPORTED.into())
}

#[tauri::command]
pub fn share_remote_control_allowed(state: tauri::State<'_, SessionState>, window_id: u32) -> bool {
    false
}

#[tauri::command]
pub async fn set_share_remote_control_allowed(
    app: tauri::AppHandle,
    state: tauri::State<'_, SessionState>,
    window_id: u32,
    allowed: bool,
) -> Result<bool, String> {
    Err(UNSUPPORTED.into())
}

#[tauri::command]
pub fn shared_window_ids(state: tauri::State<'_, SessionState>) -> Vec<u32> {
    Vec::new()
}

#[tauri::command]
pub async fn share_window(
    app: tauri::AppHandle,
    state: tauri::State<'_, SessionState>,
    window_id: u32,
    color: Option<String>,
    control_mode: Option<String>,
) -> Result<bool, ShareError> {
    Err(ShareError::Capture(UNSUPPORTED.into()))
}

#[tauri::command]
pub async fn set_share_control_mode(
    app: tauri::AppHandle,
    state: tauri::State<'_, SessionState>,
    window_id: u32,
    control_mode: Option<String>,
) -> Result<(), ShareError> {
    Err(ShareError::Capture(UNSUPPORTED.into()))
}

#[tauri::command]
pub fn share_audio_state(state: tauri::State<'_, SessionState>, window_id: u32) -> ShareAudioState {
    share_audio_state_for_state(&state, window_id)
}

pub(crate) fn share_audio_state_for_state(state: &SessionState, window_id: u32) -> ShareAudioState {
    ShareAudioState::inactive(window_id)
}

#[tauri::command]
pub async fn set_share_audio_enabled(
    app: tauri::AppHandle,
    state: tauri::State<'_, SessionState>,
    window_id: u32,
    enabled: bool,
) -> Result<ShareAudioState, String> {
    set_share_audio_enabled_for_state(&app, &state, window_id, enabled).await
}

pub(crate) async fn set_share_audio_enabled_for_state(
    app: &tauri::AppHandle,
    state: &SessionState,
    window_id: u32,
    enabled: bool,
) -> Result<ShareAudioState, String> {
    Err(UNSUPPORTED.into())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn linux_session_has_no_active_media_or_control() {
        let state = SessionState::default();
        assert!(!state.is_in_room());
        assert!(state.current_room_record().is_none());
        assert!(state.control_channel_snapshot().is_none());
        assert!(state.shared_window_ids().is_empty());
        assert!(!state.remote_control_allowed());
        assert_eq!(state.remote_control_policy(), RemoteControlPolicy::Off);
        assert!(!state.share_allows_remote_control(1));
        assert_eq!(
            state.shared_window_screen_status(1),
            SharedWindowScreenStatus::NotShared
        );
        assert!(state.mic_muted());
        assert!(state
            .set_mic_muted(false)
            .unwrap_err()
            .contains("not supported on Linux"));
        assert!(!state.camera_publishing());
        let generation = state.begin_room_generation();
        assert!(generation.is_current());
        state.invalidate_room_generation();
        assert!(!generation.is_current());
    }
}
