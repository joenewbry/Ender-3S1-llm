/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2020 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (c) 2011 Camiel Gubbels / Erik van der Zalm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include "../inc/MarlinConfig.h"

#if ENABLED(HOST_ACTION_COMMANDS)

#include "host_actions.h"

//#define DEBUG_HOST_ACTIONS

#if ENABLED(ADVANCED_PAUSE_FEATURE)
  #include "pause.h"
  #include "../gcode/queue.h"
#endif

#if HAS_FILAMENT_SENSOR
  #include "runout.h"
#endif

void host_action(PGM_P const pstr, const bool eol) {
  PORT_REDIRECT(SerialMask::All);
  SERIAL_ECHOPGM("//action:");
  SERIAL_ECHOPGM_P(pstr);
  if (eol) SERIAL_EOL();
}

#ifdef ACTION_ON_KILL
  void host_action_kill() { host_action(PSTR(ACTION_ON_KILL)); }
#endif
#ifdef ACTION_ON_PAUSE
  void host_action_pause(const bool eol/*=true*/) { host_action(PSTR(ACTION_ON_PAUSE), eol); }
#endif
#ifdef ACTION_ON_PAUSED
  void host_action_paused(const bool eol/*=true*/) { host_action(PSTR(ACTION_ON_PAUSED), eol); }
#endif
#ifdef ACTION_ON_RESUME
  void host_action_resume() { host_action(PSTR(ACTION_ON_RESUME)); }
#endif
#ifdef ACTION_ON_RESUMED
  void host_action_resumed() { host_action(PSTR(ACTION_ON_RESUMED)); }
#endif
#ifdef ACTION_ON_CANCEL
  void host_action_cancel() { host_action(PSTR(ACTION_ON_CANCEL)); }
#endif
#ifdef ACTION_ON_START
  void host_action_start() { host_action(PSTR(ACTION_ON_START)); }
#endif

#if ENABLED(HOST_PROMPT_SUPPORT)

  PGMSTR(CONTINUE_STR, "Continue");
  PGMSTR(DISMISS_STR, "Dismiss");

  #if HAS_RESUME_CONTINUE
    extern bool wait_for_user;
  #endif

  PromptReason host_prompt_reason = PROMPT_NOT_DEFINED;
  HostPromptIdentity host_prompt_identity;
  static volatile uint8_t pending_host_response = UINT8_MAX;
  static volatile uint32_t pending_host_generation = 0;

  // Emergency parser entry point: bounded mailbox only, no UI, planner, serial
  // output or token-state mutation inside the receive/interrupt context.
  bool host_response_enqueue(const uint8_t response) {
    if (response == UINT8_MAX) return false;
    bool accepted = false;
    CRITICAL_SECTION_START();
    if (pending_host_response == UINT8_MAX) {
      pending_host_generation = host_prompt_identity.generation();
      pending_host_response = response;
      accepted = true;
    }
    CRITICAL_SECTION_END();
    return accepted;
  }

  void host_response_process() {
    CRITICAL_SECTION_START();
    const uint8_t response = pending_host_response;
    const uint32_t generation = pending_host_generation;
    pending_host_response = UINT8_MAX;
    CRITICAL_SECTION_END();
    // Binding a token session disables legacy tokenless replies. Even in
    // compatibility mode, never apply a queued reply to a replacement prompt.
    if (response != UINT8_MAX && host_prompt_identity.legacy_accepts(generation))
      host_response_handler(response);
  }

  void host_action_notify(const char * const message) {
    PORT_REDIRECT(SerialMask::All);
    host_action(PSTR("notification "), false);
    SERIAL_ECHOLN(message);
  }

  void host_action_notify_P(PGM_P const message) {
    PORT_REDIRECT(SerialMask::All);
    host_action(PSTR("notification "), false);
    SERIAL_ECHOLNPGM_P(message);
  }

  void host_action_prompt(PGM_P const ptype, const bool eol=true) {
    PORT_REDIRECT(SerialMask::All);
    host_action(PSTR("prompt_"), false);
    SERIAL_ECHOPGM_P(ptype);
    if (eol) SERIAL_EOL();
  }

  void host_action_prompt_plus(PGM_P const ptype, PGM_P const pstr, const char extra_char='\0') {
    host_action_prompt(ptype, false);
    PORT_REDIRECT(SerialMask::All);
    SERIAL_CHAR(' ');
    SERIAL_ECHOPGM_P(pstr);
    if (extra_char != '\0') SERIAL_CHAR(extra_char);
    SERIAL_EOL();
  }
  void host_action_prompt_begin(const PromptReason reason, PGM_P const pstr, const char extra_char/*='\0'*/) {
    host_action_prompt_end();
    host_prompt_reason = reason;
    host_prompt_identity.begin();
    host_action_prompt_plus(PSTR("begin"), pstr, extra_char);
  }
  void host_action_prompt_button(PGM_P const pstr) {
    host_prompt_identity.button();
    host_action_prompt_plus(PSTR("button"), pstr);
  }
  void host_action_prompt_end() {
    host_prompt_identity.end();
    host_prompt_reason = PROMPT_NOT_DEFINED;
    host_action_prompt(PSTR("end"));
  }
  void host_action_prompt_show() { host_prompt_identity.show(); host_action_prompt(PSTR("show")); }

  void _host_prompt_show(PGM_P const btn1/*=nullptr*/, PGM_P const btn2/*=nullptr*/) {
    if (btn1) host_action_prompt_button(btn1);
    if (btn2) host_action_prompt_button(btn2);
    host_action_prompt_show();
  }
  void host_prompt_do(const PromptReason reason, PGM_P const pstr, PGM_P const btn1/*=nullptr*/, PGM_P const btn2/*=nullptr*/) {
    host_action_prompt_begin(reason, pstr);
    _host_prompt_show(btn1, btn2);
  }
  void host_prompt_do(const PromptReason reason, PGM_P const pstr, const char extra_char, PGM_P const btn1/*=nullptr*/, PGM_P const btn2/*=nullptr*/) {
    host_action_prompt_begin(reason, pstr, extra_char);
    _host_prompt_show(btn1, btn2);
  }

  void filament_load_host_prompt() {
    const bool disable_to_continue = TERN0(HAS_FILAMENT_SENSOR, runout.filament_ran_out);
    host_prompt_do(PROMPT_FILAMENT_RUNOUT, PSTR("Paused"), PSTR("PurgeMore"),
      disable_to_continue ? nullptr : CONTINUE_STR
    );
  }

  //
  // Handle responses from the host, such as:
  //  - Filament runout responses: Purge More, Continue
  //  - General "Continue" response
  //  - Resume Print response
  //  - Dismissal of info
  //
  bool host_response_handler(const uint8_t response) {
    const PromptReason hpr = host_prompt_reason;
    // Reject invalid/stale responses without consuming the current prompt.
    // A remote Continue must never disable a triggered filament sensor.
    switch (hpr) {
      case PROMPT_FILAMENT_RUNOUT:
        if (response > 1) return false;
        #if HAS_FILAMENT_SENSOR
          if (response == 1 && runout.filament_ran_out) return false;
        #endif
        break;
      case PROMPT_USER_CONTINUE:
      case PROMPT_PAUSE_RESUME:
      case PROMPT_INFO:
        if (response != 0) return false;
        break;
      default: return false;
    }
    host_prompt_reason = PROMPT_NOT_DEFINED;  // Reset now ahead of logic
    host_prompt_identity.end();
    switch (hpr) {
      case PROMPT_FILAMENT_RUNOUT:
        switch (response) {

          case 0: // "Purge More" button
            #if BOTH(M600_PURGE_MORE_RESUMABLE, ADVANCED_PAUSE_FEATURE)
              pause_menu_response = PAUSE_RESPONSE_EXTRUDE_MORE;  // Simulate menu selection (menu exits, doesn't extrude more)
            #endif
            break;

          case 1: // "Continue" only after the runout condition is cleared
            #if BOTH(M600_PURGE_MORE_RESUMABLE, ADVANCED_PAUSE_FEATURE)
              pause_menu_response = PAUSE_RESPONSE_RESUME_PRINT;  // Simulate menu selection
            #endif
            break;
        }
        break;
      case PROMPT_USER_CONTINUE:
        TERN_(HAS_RESUME_CONTINUE, wait_for_user = false);
        break;
      case PROMPT_PAUSE_RESUME:
        #if BOTH(ADVANCED_PAUSE_FEATURE, SDSUPPORT)
          extern const char M24_STR[];
          queue.inject_P(M24_STR);
        #endif
        break;
      case PROMPT_INFO:
        break;
      default: break;
    }
    return true;
  }

  HostPromptCheck host_token_response(const HostPromptSession session, const uint32_t prompt, const uint8_t response) {
    const HostPromptCheck checked = host_prompt_identity.check(session, prompt, response);
    if (checked != HostPromptCheck::Ready) return checked;
    #if HAS_FILAMENT_SENSOR
      if (host_prompt_reason == PROMPT_FILAMENT_RUNOUT && response == 1 && runout.filament_ran_out)
        return HostPromptCheck::PhysicalActionRequired;
    #endif
    return host_response_handler(response) ? HostPromptCheck::Ready : HostPromptCheck::UnsupportedReason;
  }

#endif // HOST_PROMPT_SUPPORT

#endif // HOST_ACTION_COMMANDS
