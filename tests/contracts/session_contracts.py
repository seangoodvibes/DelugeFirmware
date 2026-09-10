"""Architecture regressions for the squash's cross-cutting session conversions.

These checks protect routing/ownership contracts, not native UI behavior. The
checked-in manifest is reviewable; no git history or network is needed to run it.
"""

import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = json.loads(Path(__file__).with_name("squash_coverage.json").read_text())


# The parameter branch moved implementations into separately tested units.
# Keep original audit requirements attached to both halves of each move.
SOURCE_RELOCATIONS = {
    "src/deluge/model/instrument/kit.cpp": [
        "src/deluge/model/instrument/kit_param_lookup.cpp"
    ],
    "src/deluge/modulation/automation/auto_param.cpp": [
        "src/deluge/gui/views/automation/automation_interpolation.cpp"
    ],
    "src/deluge/gui/menu_item/patch_cable_strength.cpp": [
        "src/deluge/gui/menu_item/patch_cable_strength_lookup.cpp"
    ],
    "src/deluge/gui/menu_item/patched_param.cpp": [
        "src/deluge/gui/menu_item/patched_param_lookup.cpp"
    ],
    "src/deluge/gui/menu_item/unpatched_param.cpp": [
        "src/deluge/gui/menu_item/unpatched_param_lookup.cpp"
    ],
    "src/deluge/gui/views/view.cpp": ["src/deluge/gui/views/view_knob_indicator.cpp"],
    "src/deluge/gui/views/automation_view.cpp": [
        "src/deluge/gui/views/automation_view_param_lookup.cpp"
    ],
    "src/deluge/io/midi/midi_follow.cpp": [
        "src/deluge/io/midi/midi_follow_param_lookup.cpp"
    ],
}


def audited_source(path):
    return "\n".join(
        (ROOT / item).read_text() for item in [path, *SOURCE_RELOCATIONS.get(path, [])]
    )


def code_only(text):
    # Remove comments and literals so documentation cannot satisfy a code contract.
    return re.sub(
        r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
        " ",
        text,
        flags=re.DOTALL,
    )


def violations(text, entry):
    code = code_only(text)
    result = []
    for symbol in entry.get("accessors", []):
        if not re.search(r"\b" + re.escape(symbol) + r"\s*\(", code):
            result.append("missing session accessor " + symbol)
    for symbol in entry.get("forbid_raw", []):
        if re.search(
            r"\b"
            + re.escape(symbol)
            + r"\s*(?:\.|->|\[)|&\s*"
            + re.escape(symbol)
            + r"\b",
            code,
        ):
            result.append("unscoped singleton " + symbol)
    for token in entry.get("ownership", []):
        if re.sub(r"\s+", "", token) not in re.sub(r"\s+", "", code):
            result.append("missing ownership contract " + token)
    return result


class SessionRoutingContracts(unittest.TestCase):
    def test_usb_host_selects_prepared_data_before_starting_transfer(self):
        source = (ROOT / "src/deluge/io/midi/midi_engine.cpp").read_text()
        tail = code_only(
            source[
                source.index(
                    "midiDeviceNumToSendTo = deluge::io::midi::first_ready_usb_device("
                ) :
            ]
        )
        self.assertIn("device.cable[0] && device.numBytesSendingNow > 0", tail)
        self.assertRegex(tail, r"if \(midiDeviceNumToSendTo < 0\)\s*goto getOut;")
        self.assertLess(
            tail.index("goto getOut;"),
            tail.index("anyUSBSendingStillHappening[ip] = 1;"),
        )

    def test_partial_undo_discards_history_when_rollback_fails(self):
        source = (ROOT / "src/deluge/model/action/action_logger.cpp").read_text()
        method = code_only(
            source[
                source.index(
                    "PartialUndoResult ActionLogger::undoJustOneConsequencePerNoteRow("
                ) :
            ]
        )
        self.assertRegex(
            method,
            r"if \(result == PrefixResult::ROLLBACK_FAILED\)\s*deleteAllLogs\(\);",
        )
        self.assertIn("return rollback_error == Error::NONE;", method)

    def test_all_audited_files_exist_and_keep_routing(self):
        for entry in MANIFEST["files"]:
            with self.subTest(path=entry["path"]):
                path = ROOT / entry["path"]
                self.assertTrue(path.is_file())
                self.assertEqual([], violations(audited_source(entry["path"]), entry))

    def test_manifest_has_no_duplicate_paths_or_missing_evidence(self):
        paths = [e["path"] for e in MANIFEST["files"]]
        self.assertEqual(len(paths), len(set(paths)))
        self.assertEqual(MANIFEST["file_count"], len(paths))
        for entry in MANIFEST["files"]:
            self.assertIn(entry["area"], MANIFEST["areas"])
            for evidence in MANIFEST["areas"][entry["area"]]["tests"]:
                self.assertTrue((ROOT / evidence).is_file(), evidence)

    def test_contracts_detect_reverted_singletons_even_if_other_accessors_remain(self):
        mutants = 0
        for entry in MANIFEST["files"]:
            code = code_only(audited_source(entry["path"]))
            for raw in entry.get("forbid_raw", []):
                normalized = re.sub(r"_", "", raw + "ForSession").lower()
                symbol = next(
                    accessor
                    for accessor in entry.get("accessors", [])
                    if re.sub(r"_", "", accessor).lower() == normalized
                )
                changed, count = re.subn(
                    r"\b" + symbol + r"\s*\(\s*\)(?=\s*\.)", raw, code, count=1
                )
                if count:
                    with self.subTest(path=entry["path"], symbol=symbol):
                        self.assertTrue(violations(changed, entry))
                    mutants += 1
        self.assertGreater(mutants, 100)

    def test_comments_cannot_satisfy_required_contract(self):
        entry = {"accessors": ["sound_editor_for_session"], "ownership": ["Id::Local"]}
        self.assertEqual(
            2, len(violations("// sound_editor_for_session() Id::Local", entry))
        )
        self.assertEqual(
            2, len(violations("/* sound_editor_for_session() Id::Local */", entry))
        )

    def test_hardware_LED_output_requires_local_owner(self):
        for name in ["indicator_leds", "pad_leds"]:
            code = code_only((ROOT / f"src/deluge/hid/led/{name}.cpp").read_text())
            self.assertRegex(
                code,
                r"bool\s+local_output\(\)\s*\{\s*return\s+deluge::gui::ui_session::current\(\)\s*==\s*deluge::gui::ui_session::Id::Local\s*;",
            )
            self.assertRegex(code, r"if\s*\(\s*!?local_output\(\)\s*\)")

    def test_USB_filter_is_installed_at_enqueue_and_dequeue(self):
        code = code_only(
            (ROOT / "src/deluge/io/midi/midi_device_manager.cpp").read_text()
        )
        self.assertGreaterEqual(code.count("allow_usb_packet("), 2)
        self.assertGreaterEqual(code.count("is_host_client_connection(cable[0])"), 2)

    def test_mirror_action_has_both_display_labels_and_is_in_song_menu(self):
        key = "STRING_FOR_MIRROR_CONNECTED_DELUGE"
        for file, label in [
            ("english.json", "Mirror Connected Deluge"),
            ("seven_segment.json", "MIRR"),
        ]:
            values = json.loads((ROOT / "src/deluge/gui/l10n" / file).read_text())
            self.assertEqual(label, values["strings"][key])
        for file in ["strings.h", "g_english.cpp", "g_seven_segment.cpp"]:
            self.assertIn(key, (ROOT / "src/deluge/gui/l10n" / file).read_text())
        menu = code_only((ROOT / "src/deluge/gui/ui/menus.cpp").read_text())
        self.assertRegex(menu, r"&mirror_connected_deluge_menu")
        action = code_only(
            (ROOT / "src/deluge/gui/menu_item/song/mirror.h").read_text()
        )
        self.assertIn("hid::mirror::start()", action)

    def test_USB_queue_fixture_sizes_match_production_constants(self):
        header = code_only(
            (ROOT / "src/deluge/io/midi/midi_device_manager.h").read_text()
        )
        for name, value in [
            ("MIDI_SEND_BUFFER_LEN_RING", 1024),
            ("MIDI_SEND_BUFFER_LEN_INNER", 32),
            ("MIDI_SEND_BUFFER_LEN_INNER_HOST", 2),
        ]:
            self.assertRegex(header, rf"#define\s+{name}\s+{value}\b")

    def test_shift_preflight_and_history_reservation_precede_mutation(self):
        code = code_only((ROOT / "src/deluge/gui/views/clip_view.cpp").read_text())
        shift = code[code.index("horizontal_shift_amount(") :]
        self.assertLess(
            shift.index("can_shift_horizontally("), shift.index("getNewAction(")
        )
        self.assertLess(
            shift.index("allocLowSpeed(sizeof(ConsequenceClipHorizontalShift))"),
            shift.index("clip->shiftHorizontally("),
        )
        self.assertIn("history_unchanged()", shift)
        clip = code_only((ROOT / "src/deluge/model/clip/clip.h").read_text())
        self.assertRegex(
            clip, r"can_shift_horizontally\([^)]*\)\s*\{\s*return loopLength > 0;"
        )

    def test_output_publication_and_array_safety_guards_remain_installed(self):
        oled = code_only((ROOT / "src/deluge/hid/display/oled.cpp").read_text())
        self.assertRegex(
            oled,
            r"Id::Remote\)\s*\{\s*frame_states.active\(\).publish\(\);\s*needs_sending_for_session\(\) = false;\s*return;",
        )
        array = code_only(
            (
                ROOT / "src/deluge/util/container/array/ordered_resizeable_array.cpp"
            ).read_text()
        )
        method = array[
            array.index("void OrderedResizeableArrayWith32bitKey::shiftHorizontal") :
        ]
        self.assertLess(
            method.index("effectiveLength <= 0"),
            method.index("shiftAmount %= effectiveLength"),
        )
        self.assertNotIn("-shiftAmount)", method[: method.index("int32_t cutoffPos")])

    def test_note_row_identity_survives_relocation_but_is_renewed_on_clone(self):
        row = code_only((ROOT / "src/deluge/model/note/note_row.h").read_text())
        self.assertIn(
            "uint64_t undo_identity = deluge::model::next_note_row_identity();", row
        )
        self.assertLess(row.index("int16_t y;"), row.index("uint64_t undo_identity"))
        clip = code_only(
            (ROOT / "src/deluge/model/clip/instrument_clip.cpp").read_text()
        )
        renewal = clip.index(
            "noteRow->undo_identity = deluge::model::next_note_row_identity();"
        )
        self.assertLess(clip.index("newClip->noteRows.cloneFrom(&noteRows)"), renewal)
        self.assertLess(renewal, clip.index("noteRow->beenCloned("))

    def test_clip_deletion_preflight_precedes_target_access_and_mutation(self):
        code = code_only(
            (
                ROOT / "src/deluge/model/consequence/consequence_clip_existence.cpp"
            ).read_text()
        )
        method = code[code.index("Error ConsequenceClipExistence::revert(") :]
        check = method.index("get_clip_index_for_undo(clipArray, clip)")
        self.assertLess(check, method.index("addTimelineCounter(clip)"))
        self.assertLess(check, method.index("invalidate_clip_selection(clip)"))
        self.assertLess(check, method.index("clip->stopAllNotesPlaying("))
        self.assertIn("!modelStack || !modelStack->song || !clip", method[:check])
        deletion = method[
            method.index("clipIndex = modelStack->song->get_clip_index_for_undo") :
        ]
        self.assertRegex(deletion, r"if \(clipIndex == -1\)\s*\{\s*return Error::BUG;")

    def test_clip_recreation_preflight_precedes_reservation_and_reattachment(self):
        code = code_only(
            (
                ROOT / "src/deluge/model/consequence/consequence_clip_existence.cpp"
            ).read_text()
        )
        method = code[code.index("Error ConsequenceClipExistence::revert(") :]
        check = method.index("reserve_for_recreation(modelStack->song)")
        self.assertLess(check, method.index("reattach_for_recreation("))

    def test_clip_restoration_commits_before_publishing_ownership(self):
        code = code_only(
            (
                ROOT / "src/deluge/model/consequence/consequence_clip_existence.cpp"
            ).read_text()
        )
        method = code[code.index("Error ConsequenceClipExistence::revert(") :]
        commit = method.index("error = commit_recreation(modelStack->song)")
        self.assertLess(commit, method.index("notify_peer_clip_inserted(clipIndex)"))
        self.assertRegex(
            method[commit:],
            r"commit_recreation\(modelStack->song\);\s*if \(error != Error::NONE\)\s*return error;",
        )
        self.assertNotIn("insertClipAtIndex(clip, clipIndex)", method)

    def test_undo_parameter_reattachment_requires_exact_clip_backups(self):
        cases = [
            (
                "clip.cpp",
                "Error Clip::undoDetachmentFromOutput(",
                "// ----- TimelineCounter",
                "E245",
            ),
            (
                "instrument_clip.cpp",
                "Error InstrumentClip::undoUnassignmentOfAllNoteRowsFromDrums(",
                "// Do *not* use",
                "E229",
            ),
        ]
        for filename, start, end, freeze in cases:
            source = (ROOT / "src/deluge/model/clip" / filename).read_text()
            body = source[source.index(start) :]
            body = body[: body.index(end)]
            code = code_only(body)
            self.assertIn("getBackedUpParamManagerForExactClip(", code)
            self.assertNotIn("getBackedUpParamManagerPreferablyWithClip(", code)
            self.assertIn("return Error::BUG;", code)
            self.assertNotIn(f'FREEZE_WITH_ERROR("{freeze}")', body)
        consequence = (
            ROOT / "src/deluge/model/consequence/consequence_clip_existence.cpp"
        ).read_text()
        self.assertNotIn('FREEZE_WITH_ERROR("E046")', consequence)

    def test_kit_backup_preflight_precedes_row_transfers(self):
        source = (ROOT / "src/deluge/model/clip/instrument_clip.cpp").read_text()
        method = code_only(
            source[source.index("Error InstrumentClip::undoDetachmentFromOutput(") :]
        )
        check = method.index("getBackedUpParamManagerForExactClip(")
        self.assertLess(
            check, method.index("undoUnassignmentOfAllNoteRowsFromDrums(modelStack)")
        )
        self.assertIn(
            "return Error::BUG;",
            method[
                check : method.index(
                    "undoUnassignmentOfAllNoteRowsFromDrums(modelStack)"
                )
            ],
        )

    def test_note_edit_callers_stop_when_history_recording_fails(self):
        code = code_only((ROOT / "src/deluge/model/note/note_row.cpp").read_text())
        calls = list(re.finditer(r"action->recordNoteExistenceChange\(", code))
        self.assertEqual(len(calls), 3)
        calls = calls[:2]
        for call in calls:
            with self.subTest(offset=call.start()):
                self.assertRegex(
                    code[max(0, call.start() - 8) : call.start()], r"if\s*\(\s*$"
                )
                self.assertRegex(
                    code[call.start() :],
                    r"^action->recordNoteExistenceChange\([\s\S]*?ExistenceChangeType::"
                    r"CREATE,\s*&newNote\)\s*!=\s*Error::NONE\)\s*return(?:\s+0)?;",
                )

    def test_note_deletion_errors_reach_ui_callers(self):
        row = code_only((ROOT / "src/deluge/model/note/note_row.cpp").read_text())
        for method, call in [
            ("deleteNoteByPos", "deleteNoteByIndex"),
            ("deleteNoteByIndex", "action->recordNoteExistenceChange"),
        ]:
            body = row[row.index("Error NoteRow::" + method) :]
            self.assertRegex(
                body,
                r"Error\s+error\s*=\s*"
                + re.escape(call)
                + r"\([^;]+;\s*if \(error != Error::NONE\)\s*return error;",
            )
        ui = code_only(
            (ROOT / "src/deluge/gui/views/instrument_clip_view.cpp").read_text()
        )
        calls = list(re.finditer(r"(?:thisNoteRow|sourceRow)->deleteNoteByPos\(", ui))
        self.assertEqual(len(calls), 2)
        for call in calls:
            self.assertRegex(
                ui[max(0, call.start() - 8) : call.start()], r"if\s*\(\s*$"
            )
            self.assertRegex(
                ui[call.start() :], r"^[^;]+?!= Error::NONE\)\s*return(?: false)?;"
            )

    def test_scroll_stops_when_grabbing_notes_fails(self):
        ui = code_only(
            (ROOT / "src/deluge/gui/views/instrument_clip_view.cpp").read_text()
        )
        self.assertRegex(
            ui,
            r"if \(!scrollVertical_grabNotesPressed\(modelStack, clip\)\)\s*return ActionResult::DEALT_WITH;",
        )

    def test_note_array_edit_callers_stop_on_snapshot_failure(self):
        code = code_only((ROOT / "src/deluge/model/note/note_row.cpp").read_text())
        calls = list(
            re.finditer(
                r"action->recordNoteArrayChange(?:Definitely|IfNotAlreadySnapshotted)\(",
                code,
            )
        )
        self.assertEqual(len(calls), 13)
        for call in calls:
            with self.subTest(offset=call.start()):
                assignment = re.search(
                    r"Error\s+(\w+)\s*=\s*$",
                    code[max(0, call.start() - 80) : call.start()],
                )
                self.assertIsNotNone(assignment)
                result = assignment.group(1)
                end = code.index(";", call.start()) + 1
                self.assertRegex(
                    code[end:],
                    r"^\s*if\s*\("
                    + result
                    + r"\s*!=\s*Error::NONE\)\s*\{?\s*"
                    + r"(?:delugeDealloc\(searchTerms\);\s*)?return(?:\s+\w+)?;",
                )

    def test_repeat_snapshot_precedes_parameter_mutation(self):
        code = code_only((ROOT / "src/deluge/model/note/note_row.cpp").read_text())
        body = code[code.index("bool NoteRow::generateRepeats") :]
        self.assertLess(
            body.index("return false;"), body.index("paramManager.generateRepeats")
        )

    def test_trim_and_length_failures_stop_outer_callers(self):
        row = code_only((ROOT / "src/deluge/model/note/note_row.cpp").read_text())
        self.assertRegex(
            row,
            r"Error error = trimToLength\(newLength, modelStack, actionToRecordTo\);\s*if \(error != Error::NONE\)\s*return error;",
        )
        self.assertRegex(
            row,
            r"if \(complexSetNoteLength\(note, newLength, modelStack, action\) != Error::NONE\)\s*return 0;",
        )
        body = row[row.index("int32_t edited_pos = note->pos;") :]
        self.assertLess(
            body.index("notes.search(edited_pos"),
            body.index("*firstNote = *lastNote = note;"),
        )
        clip = code_only(
            (ROOT / "src/deluge/model/clip/instrument_clip.cpp").read_text()
        )
        calls = list(re.finditer(r"thisNoteRow->trimToLength\(", clip))
        self.assertEqual(len(calls), 2)
        for call in calls:
            self.assertRegex(
                clip[max(0, call.start() - 8) : call.start()], r"if\s*\(\s*$"
            )
            self.assertRegex(
                clip[call.start() :], r"^[^;]+?!= Error::NONE\)\s*return(?: false)?;"
            )

    def test_clip_repeat_callers_stop_when_row_generation_fails(self):
        code = code_only(
            (ROOT / "src/deluge/model/clip/instrument_clip.cpp").read_text()
        )
        calls = list(re.finditer(r"thisNoteRow->generateRepeats\(", code))
        self.assertEqual(len(calls), 2)
        for call in calls:
            self.assertRegex(
                code[max(0, call.start() - 8) : call.start()], r"if\s*\(!\s*$"
            )
            self.assertRegex(code[call.start() :], r"^[^;]+\)\)\s*return(?: false)?;")

    def test_arrangement_rejects_failed_repeat_before_installation(self):
        code = code_only(
            (ROOT / "src/deluge/playback/mode/arrangement.cpp").read_text()
        )
        body = code[code.index("Error Arrangement::doUniqueCloneOnClipInstance(") :]
        check = body.index("if (!repeated || !source_valid()")
        cleanup = body.index(
            "owner->deleteClipObject(newClip, false, InstrumentRemoval::NONE)", check
        )
        failure = body.index("return Error::BUG;", cleanup)
        install = body.index("arrangementOnlyClips.insertClipAtIndex", failure)
        self.assertLess(check, cleanup)
        self.assertLess(failure, install)
        for guard in (
            "currentSong == owner",
            "modelStack->song == owner",
            "== local_revision",
            "== remote_revision",
            "getTimelineCounterAllowNull() == newClip",
            "!owner->contains_clip_for_undo(newClip)",
        ):
            self.assertIn(guard, body[check:cleanup])

    def test_row_length_rejects_nonpositive_before_modulo(self):
        code = code_only((ROOT / "src/deluge/model/note/note_row.cpp").read_text())
        body = code[code.index("Error NoteRow::setLength(") :]
        self.assertRegex(body, r"if \(newLength <= 0\)\s*return Error::BUG;")
        self.assertLess(body.index("return Error::BUG;"), body.index("oldPos %"))

    def test_ui_row_resize_errors_stop_before_feedback(self):
        code = code_only(
            (ROOT / "src/deluge/gui/views/instrument_clip_view.cpp").read_text()
        )
        for call in (
            "noteRow->setLength(modelStack, newLength, prevAction",
            "newConsequence->performChange(modelStack, action",
        ):
            start = code.index(call)
            self.assertRegex(
                code[start:],
                r"^[^;]+;\s*if \(error != Error::NONE\)\s*\{\s*display->displayError\(error\);\s*return;",
            )

    def test_failed_ui_multiply_stops_before_success_feedback(self):
        code = code_only(
            (ROOT / "src/deluge/gui/views/instrument_clip_view.cpp").read_text()
        )
        call = code.index(
            "currentSong->doubleClipLength(getCurrentInstrumentClip(), action)"
        )
        self.assertRegex(code[call - 5 : call], r"if\s*\(!\s*$")
        self.assertRegex(code[call:], r"^[^;]+\)\s*return;\s*zoomToMax")

    def test_independent_mode_is_not_negotiated(self):
        code = code_only((ROOT / "src/deluge/hid/mirror_protocol.h").read_text())
        ops = re.search(r"enum class Op[^\{]*\{([^}]+)", code).group(1)
        self.assertEqual(
            [
                "Request",
                "Accept",
                "Stop",
                "Heartbeat",
                "Panel",
                "OLED",
                "Input",
                "SyncLED",
                "InputAck",
                "capability_query",
                "capabilities",
                "request_rejected",
            ],
            [s.strip() for s in ops.split(",") if s.strip()],
        )
        runtime = code_only((ROOT / "src/deluge/hid/mirror.cpp").read_text())
        self.assertIn(
            "!protocol::supports_session_mode(session_request->mode)", runtime
        )

    def test_audio_marker_callers_stop_on_failure(self):
        source = code_only(
            (ROOT / "src/deluge/gui/views/audio_clip_view.cpp").read_text()
        )
        calls = re.findall(
            r"(?<!::)changeUnderlyingSample(?:Start|Length)\([^;{}]*?;", source
        )
        self.assertEqual(3, len(calls))
        guarded = re.findall(
            r"if\s*\(\s*!changeUnderlyingSample(?:Start|Length)\([^;{}]*?\)\s*\)"
            r"\s*return ActionResult::DEALT_WITH;",
            source,
        )
        self.assertEqual(3, len(guarded))

    def test_recording_stop_and_note_entry_abort_after_resize_failure(self):
        cases = [
            (
                "src/deluge/playback/mode/session.cpp",
                "void Session::toggleClipStatus(",
                "clip, clip->getLivePos() + 1, action, false",
                "armClipToStopAction(clip)",
            ),
            (
                "src/deluge/gui/views/instrument_clip_view.cpp",
                "void InstrumentClipView::editPadAction(",
                "clip, desiredNoteLength, action",
                "whichRowsToReRender = 0xFFFFFFFF",
            ),
        ]
        for path, method, arguments, following in cases:
            with self.subTest(path=path):
                source = code_only((ROOT / path).read_text())
                body = source[source.index(method) :]
                normalized = re.sub(r"\s+", " ", body)
                call = "currentSong->setClipLength(" + arguments + ")"
                self.assertIn("if (!" + call + ") return;", normalized)
                tail = normalized[normalized.index(call) :]
                self.assertLess(tail.index("return;"), tail.index(following))


if __name__ == "__main__":
    unittest.main()
