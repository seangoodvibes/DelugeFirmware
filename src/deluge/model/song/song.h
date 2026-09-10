/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "gui/menu_item/reverb/model.h"
#include "gui/ui/ui_navigation_state.h"
#include "io/midi/learned_midi.h"
#include "model/clip/clip.h"
#include "model/clip/clip_array.h"
#include "model/global_effectable/global_effectable_for_song.h"
#include "model/instrument/instrument.h"
#include "model/output.h"
#include "model/scale/musical_key.h"
#include "model/scale/note_set.h"
#include "model/scale/preset_scales.h"
#include "model/scale/scale_change.h"
#include "model/scale/scale_mapper.h"
#include "model/song/song_clip_selection.h"
#include "model/song/song_navigation_state.h"
#include "model/sync.h"
#include "model/timeline_counter.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "storage/flash_storage.h"
#include "util/container/array/ordered_resizeable_array_with_multi_word_key.h"
#include "util/container/retained_list.h"
#include "util/d_string.h"

class MidiCommand;
class Clip;
class AudioClip;
class Instrument;
class InstrumentClip;
class Synth;
class ParamManagerForTimeline;
class Instrument;
class ParamManagerForTimeline;
class Drum;
class SoundInstrument;
class SoundDrum;
class Action;
class ArrangementRow;
class BackedUpParamManager;
class ArpeggiatorSettings;
class Kit;
class MIDIInstrument;
class NoteRow;
class Output;
class AudioOutput;
class ModelStack;
class ModelStackWithTimelineCounter;
Clip* getCurrentClip();
InstrumentClip* getCurrentInstrumentClip();
AudioClip* getCurrentAudioClip();
Output* getCurrentOutput();
Kit* getCurrentKit();
Instrument* getCurrentInstrument();
OutputType getCurrentOutputType();

class Section {
public:
	LearnedMIDI launchMIDICommand;
	int16_t numRepetitions;

	Section() { numRepetitions = 0; }
};

struct BackedUpParamManager {
	ModControllableAudio* modControllable;
	Clip* clip;
	ParamManager paramManager;
};

#define MAX_NOTES_CHORD_MEM 10

enum SessionMacroKind : int8_t {
	NO_MACRO = 0,
	CLIP_LAUNCH,
	OUTPUT_CYCLE,
	SECTION,
	NUM_KINDS,
};

struct SessionMacro {
	SessionMacroKind kind{NO_MACRO};
	Clip* clip{nullptr};
	Output* output{nullptr};
	uint8_t section{0};
};

class Song final : public TimelineCounter {
public:
	Song();
	~Song() override;
	bool mayDoubleTempo();
	bool ensureAtLeastOneSessionClip();
	void transposeAllScaleModeClips(int32_t interval);
	void transposeAllScaleModeClips(int32_t offset, bool chromatic);
	bool anyScaleModeClips();
	void changeMusicalMode(uint8_t yVisualWithinOctave, int8_t change);
	void rotateMusicalMode(int8_t change);
	void replaceMusicalMode(const ScaleChange& changes, bool affectMIDITranspose);
	int32_t getYVisualFromYNote(int32_t yNote, bool inKeyMode);
	int32_t getYVisualFromYNote(int32_t yNote, bool inKeyMode, const MusicalKey& key);
	int32_t incrementYNoteInKey(int32_t yNote, int32_t increment, bool inOctave, bool inKey) const;
	static int32_t incrementYNoteInKey(int32_t yNote, int32_t increment, bool inOctave, const MusicalKey& key);
	static int32_t incrementYNoteNoKey(int32_t yNote, int32_t increment, bool inOctave, const MusicalKey& key);
	int32_t getYNoteFromYVisual(int32_t yVisual, bool inKeyMode);
	int32_t getYNoteFromYVisual(int32_t yVisual, bool inKeyMode, const MusicalKey& key);
	bool mayMoveModeNote(int16_t yVisualWithinOctave, int8_t newOffset);
	ParamManagerForTimeline* findParamManagerForDrum(Kit* kit, Drum* drum, Clip* stopTraversalAtClip = nullptr);
	void setupPatchingForAllParamManagersForDrum(SoundDrum* drum);
	void setupPatchingForAllParamManagersForInstrument(SoundInstrument* sound);
	void grabVelocityToLevelFromMIDICableAndSetupPatchingForAllParamManagersForInstrument(MIDICable& cable,
	                                                                                      SoundInstrument* instrument);
	void grabVelocityToLevelFromMIDICableAndSetupPatchingForAllParamManagersForDrum(MIDICable& cable, SoundDrum* drum,
	                                                                                Kit* kit);
	void grabVelocityToLevelFromMIDICableAndSetupPatchingForEverything(MIDICable& cable);
	void getCurrentRootNoteAndScaleName(StringBuf& buffer);
	void displayCurrentRootNoteAndScaleName();

	// Scale-related methods

	/// Changes to next applicable scale.
	Scale cycleThroughScales();
	/// Returns current scale
	Scale getCurrentScale();
	/// Changes to requested scale, will return the scale if successfull, or NO_SCALE if change
	/// could not be completed.
	Scale setScale(Scale newScale);
	/// Changes scale to the requested notes. Returns true if successful. If the notes are not a preset
	/// scale, defines the scale as the new user scale.
	bool setScaleNotes(NoteSet newScale);
	/// Learns a user scale from notes in scale mode clips.
	void learnScaleFromCurrentNotes();
	/// Returns true if the song has a user scale.
	bool hasUserScale();
	/// Sets root note of key. If the previous scale no longer fits, changes to a new implied scale, which
	/// can result in a new user scale being set.
	void setRootNote(int32_t newRootNote, InstrumentClip* clipToAvoidAdjustingScrollFor = nullptr);
	/// Returns a NoteSet with all notes currently in used in scale mdoe clips.
	NoteSet notesInScaleModeClips();

	void setTempoFromNumSamples(double newTempoSamples, bool shouldLogAction);
	void setupDefault();
	void setBPM(float tempoBPM, bool shouldLogAction);
	void setTempoFromParams(int32_t magnitude, int8_t whichValue, bool shouldLogAction);
	void deleteSoundsWhichWontSound();
	void writeTemplateSong(const char* templateSong);
	void
	deleteClipObject(Clip* clip, bool songBeingDestroyedToo = false,
	                 InstrumentRemoval instrumentRemovalInstruction = InstrumentRemoval::DELETE_OR_HIBERNATE_IF_UNUSED);
	int32_t getMaxMIDIChannelSuffix(int32_t channel);
	void addOutput(Output* output, bool atStart = true);
	void deleteOutputThatIsInMainList(Output* output, bool stopAnyAuditioningFirst = true);
	void markAllInstrumentsAsEdited();
	Instrument* getInstrumentFromPresetSlot(OutputType outputType, int32_t presetNumber, int32_t presetSubSlotNumber,
	                                        char const* name, char const* dirPath, bool searchHibernatingToo = true,
	                                        bool searchNonHibernating = true);
	AudioOutput* getAudioOutputFromName(std::string_view name);
	void setupPatchingForAllParamManagers();
	void replaceInstrument(Instrument* oldInstrument, Instrument* newInstrument, bool keepNoteRowsWithMIDIInput = true);
	void stopAllMIDIAndGateNotesPlaying();
	void stopAllAuditioning();
	void deleteOrHibernateOutput(Output* output);
	Instrument* getNonAudioInstrumentToSwitchTo(OutputType newOutputType, Availability availabilityRequirement,
	                                            int16_t newSlot, int8_t newSubSlot, bool* instrumentWasAlreadyInSong);
	void notify_peer_clip_inserted(int32_t index) {
		navigation.peer_clip_inserted(index);
		deluge::gui::ui_session::request_peer_structural_refresh();
	}
	void notify_peer_clip_removed(int32_t index) {
		navigation.peer_clip_removed(index);
		deluge::gui::ui_session::request_peer_structural_refresh();
	}
	void notify_peer_clips_swapped(int32_t first, int32_t second) {
		navigation.peer_clips_swapped(first, second);
		deluge::gui::ui_session::request_peer_structural_refresh();
	}
	void removeSessionClipLowLevel(Clip* clip, int32_t clipIndex);
	void changeSwingInterval(int32_t newValue);
	int32_t convertSyncLevelFromFileValueToInternalValue(int32_t fileValue);
	int32_t convertSyncLevelFromInternalValueToFileValue(int32_t internalValue);
	String getSongFullPath();
	void setSongFullPath(const char* fullPath);
	int32_t getInputTickMagnitude() const { return insideWorldTickMagnitude + insideWorldTickMagnitudeOffsetFromBPM; }

	GlobalEffectableForSong globalEffectable;

	ClipArray sessionClips;
	ClipArray arrangementOnlyClips;

	bool contains_clip_for_undo(const Clip* clip);
	int32_t get_clip_index_for_undo(ClipArray* array, const Clip* clip);
	bool can_reference_clip_from_output(const Clip* clip, const Output* output);
	bool owns_output_for_undo(const Output* output, bool include_detached = true) const;
	void retain_output_for_undo(Output* output) { undo_detached_outputs.retain(output); }
	bool is_output_retained_for_undo(const Output* output) const { return undo_detached_outputs.contains(output); }
	Output* firstOutput;
	Instrument*
	    firstHibernatingInstrument; // All Instruments have inValidState set to false when they're added to this list

	OrderedResizeableArrayWithMultiWordKey backedUpParamManagers;

	auto& x_scroll_for_session() { return navigation.active().xScroll; }
	auto& x_zoom_for_session() { return navigation.active().xZoom; }
	auto& x_scroll_for_return_to_song_view_for_session() { return navigation.active().xScrollForReturnToSongView; }
	auto& x_zoom_for_return_to_song_view_for_session() { return navigation.active().xZoomForReturnToSongView; }

	auto& triplets_on_for_session() { return navigation.active().tripletsOn; }
	auto& triplets_level_for_session() {
		return navigation.active().tripletsLevel;
	} // The number of ticks in one of the three triplets

	uint64_t timePerTimerTickBig;
	int32_t divideByTimePerTimerTick;

	// How many orders of magnitude faster internal ticks are going than input ticks. Used in combination with
	// inputTickScale, which is usually 1, but is different if there's an inputTickScaleClip. So, e.g. if
	// insideWorldTickMagnitude is 1, this means the inside world is spinning twice as fast as the external world, so
	// MIDI sync coming in representing an 8th-note would be interpreted internally as a quarter-note (because two
	// internal 8th-notes would have happened, twice as fast, making a quarter-note)
	int32_t insideWorldTickMagnitude;

	// Sometimes, we'll do weird stuff to insideWorldTickMagnitude for sync-scaling, which would make BPM values look
	// weird. So, we keep insideWorldTickMagnitudeOffsetFromBPM
	int32_t insideWorldTickMagnitudeOffsetFromBPM;

	int8_t swingAmount;
	uint8_t swingInterval;

	Section sections[kMaxNumSections];

	MusicalKey key;
	std::bitset<NUM_PRESET_SCALES> disabledPresetScales;

	String name;

	bool& affect_entire_for_session() { return navigation.active().affectEntire; }
	bool affect_entire_for_session() const { return navigation.active().affectEntire; }

	SessionLayoutType& session_layout_for_session() { return navigation.active().sessionLayout; }
	SessionLayoutType session_layout_for_session() const { return navigation.active().sessionLayout; }
	auto& song_grid_scroll_x_for_session() { return navigation.active().songGridScrollX; }
	auto& song_grid_scroll_y_for_session() { return navigation.active().songGridScrollY; }
	auto& song_view_y_scroll_for_session() { return navigation.active().songViewYScroll; }
	auto& arrangement_y_scroll_for_session() { return navigation.active().arrangementYScroll; }

	uint8_t sectionToReturnToAfterSongEnd;

	bool wasLastInArrangementEditor;
	auto& last_clip_instance_entered_start_pos_for_session() {
		return navigation.active().lastClipInstanceEnteredStartPos;
	} // -1 means we are not "inside" an arrangement. While we're in the
	  // ArrangementEditor, it's 0

	auto& arranger_auto_scroll_mode_active_for_session() { return navigation.active().arrangerAutoScrollModeActive; }

	MIDIInstrument* hibernatingMIDIInstrument;

	bool outputClipInstanceListIsCurrentlyInvalid; // Set to true during scenarios like replaceInstrument(), to warn
	                                               // other functions not to look at Output::clipInstances

	bool paramsInAutomationMode;

	bool inClipMinderViewOnLoad; // Temp variable only valid while loading Song

	int32_t unautomatedParamValues[deluge::modulation::params::kMaxNumUnpatchedParams];

	String dirPath;

	std::array<SessionMacro, 8> sessionMacros{};

	bool getAnyClipsSoloing() const;
	Clip* getCurrentClip();
	void setCurrentClip(Clip* clip) { clip_selection.select(clip); }
	void invalidate_clip_selection(Clip* clip) { clip_selection.replace(clip, nullptr); }

	uint32_t getInputTickScale();
	Clip* getSyncScalingClip();
	void setInputTickScaleClip(Clip* clip);
	inline bool isFillModeActive() { return fillModeActive; }
	void changeFillMode(bool on);
	void loadNextSong();
	bool setClipLength(Clip* clip, uint32_t newLength, Action* action, bool mayReSyncClip = true);
	bool doubleClipLength(InstrumentClip* clip, Action* action = nullptr);
	Clip* getClipWithOutput(Output* output, bool mustBeActive = false, Clip* excludeClip = nullptr,
	                        bool require_compatible_param_manager = false);
	Error readFromFile(Deserializer& reader);
	void writeToFile();
	void loadAllSamples(bool mayActuallyReadFiles = true);
	void renderAudio(std::span<StereoSample> outputBuffer, int32_t* reverbBuffer, int32_t sideChainHitPending);
	bool isYNoteAllowed(int32_t yNote, bool inKeyMode);
	Clip* syncScalingClip;
	void setTimePerTimerTick(uint64_t newTimeBig, bool shouldLogAction = false);
	bool hasAnySwing();
	void resyncLFOs();
	void ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(Sound* sound);
	bool areAllClipsInSectionPlaying(int32_t section);
	void removeYNoteFromMode(int32_t yNoteWithinOctave);
	void turnSoloingIntoJustPlaying(bool getRidOfArmingToo = true);
	void reassessWhetherAnyClipsSoloing();
	float getTimePerTimerTickFloat();
	uint32_t getTimePerTimerTickRounded();
	int32_t getNumOutputs();
	Clip* getNextSessionClipWithOutput(int32_t offset, Output* output, Clip* prevClip);
	bool anyClipsSoloing;

	ParamManager* getBackedUpParamManagerForExactClip(ModControllableAudio* modControllable, Clip* clip,
	                                                  ParamManager* stealInto = nullptr);
	ParamManager* getBackedUpParamManagerPreferablyWithClip(ModControllableAudio* modControllable, Clip* clip,
	                                                        ParamManager* stealInto = nullptr);
	void backUpParamManager(ModControllableAudio* modControllable, Clip* clip, ParamManagerForTimeline* paramManager,
	                        bool shouldStealExpressionParamsToo = false);
	void moveInstrumentToHibernationList(Instrument* instrument);
	void deleteOrHibernateOutputIfNoClips(Output* output);
	void removeInstrumentFromHibernationList(Instrument* instrument);
	bool doesOutputHaveActiveClipInSession(Output* output);
	bool doesNonAudioSlotHaveActiveClipInSession(OutputType outputType, int32_t slot, int32_t subSlot = -1);
	bool doesNonAudioSlotHaveClipInSession(OutputType outputType, int32_t slot, int32_t subSlot);
	bool doesOutputHaveAnyClips(Output* output);
	void deleteBackedUpParamManagersForClip(Clip* clip);
	void deleteBackedUpParamManagersForModControllable(ModControllableAudio* modControllable);
	void deleteHibernatingInstrumentWithSlot(OutputType outputType, char const* name);
	void loadCrucialSamplesOnly();
	Clip* getSessionClipWithOutput(Output* output, int32_t requireSection = -1, Clip* excludeClip = nullptr,
	                               int32_t* clipIndex = nullptr, bool excludePendingOverdubs = false);
	void restoreClipStatesBeforeArrangementPlay();
	void deleteOrAddToHibernationListOutput(Output* output);
	int32_t getLowestSectionWithNoSessionClipForOutput(Output* output);
	void assertActiveness(ModelStackWithTimelineCounter* modelStack, int32_t endInstanceAtTime = -1);
	[[nodiscard]] bool isClipActive(Clip const* clip) const;
	void sendAllMIDIPGMs();
	void sortOutWhichClipsAreActiveWithoutSendingPGMs(ModelStack* modelStack, int32_t playbackWillStartInArrangerAtPos);
	void deactivateAnyArrangementOnlyClips();
	Clip* getLongestClip(bool includePlayDisabled, bool includeArrangementOnly);
	Clip* getLongestActiveClipWithMultipleOrFactorLength(int32_t targetLength, bool revertToAnyActiveClipIfNone = true,
	                                                     Clip* excludeClip = nullptr);
	int32_t getOutputIndex(Output* output);
	void setHibernatingMIDIInstrument(MIDIInstrument* newInstrument);
	void deleteHibernatingMIDIInstrument();
	MIDIInstrument* grabHibernatingMIDIInstrument(int32_t newSlot, int32_t newSubSlot);
	NoteRow* findNoteRowForDrum(Kit* kit, Drum* drum, Clip* stopTraversalAtClip = nullptr,
	                            bool requireCompatibleParamManager = false);

	bool anyOutputsSoloingInArrangement;
	bool getAnyOutputsSoloingInArrangement();
	void reassessWhetherAnyOutputsSoloingInArrangement();
	bool isOutputActiveInArrangement(Output* output);
	Output* getOutputFromIndex(int32_t index);
	void ensureAllInstrumentsHaveAClipOrBackedUpParamManager(char const* errorMessageNormal,
	                                                         char const* errorMessageHibernating);
	Error placeFirstInstancesOfActiveClips(int32_t pos);
	void endInstancesOfActiveClips(int32_t pos, bool detachClipsToo = false);
	Error clearArrangementBeyondPos(int32_t pos, Action* action);
	Error deletingClipInstanceForClip(Output* output, Clip* clip, Action* action, bool shouldPickNewActiveClip,
	                                  bool preserve_history_on_failure = false);
	bool arrangementHasAnyClipInstances();
	void resumeClipsClonedForArrangementRecording();
	void setParamsInAutomationMode(bool newState);
	bool shouldOldOutputBeReplaced(Clip* clip, Availability* availabilityRequirement = nullptr);
	Output* navigateThroughPresetsForInstrument(Output* output, int32_t offset);
	void instrumentSwapped(Instrument* newInstrument);
	Instrument* changeOutputType(Instrument* oldInstrument, OutputType newOutputType);
	AudioOutput* getFirstAudioOutput();
	AudioOutput* createNewAudioOutput(Output* replaceOutput = nullptr);
	/// buffer must have at least 5 characters on 7seg, or 30 for OLED
	void getNoteLengthName(StringBuf& buffer, uint32_t noteLength, char const* notesString = "-notes",
	                       bool clarifyPerColumn = false) const;
	void replaceOutputLowLevel(Output* newOutput, Output* oldOutput);
	void removeSessionClip(Clip* clip, int32_t clipIndex, bool forceClipsAboveToMoveVertically = false);
	bool deletePendingOverdubs(Output* onlyWithOutput = nullptr, int32_t* originalClipIndex = nullptr,
	                           bool createConsequencesForOtherLinearlyRecordingClips = false);
	Clip* getPendingOverdubWithOutput(Output* output);
	Clip* getClipWithOutputAboutToBeginLinearRecording(Output* output);
	Clip* createPendingNextOverdubBelowClip(Clip* clip, int32_t clipIndex, OverDubType newOverdubNature);
	bool hasAnyPendingNextOverdubs();
	Output* getNextAudioOutput(int32_t offset, Output* oldOutput, Availability availabilityRequirement);
	void deleteOutput(Output* output);
	void clearRecordingFromReferencesTo(Output* output);
	void cullAudioClipVoice();
	int32_t getYScrollSongViewWithoutPendingOverdubs();
	int32_t removeOutputFromMainList(Output* output, bool stopAnyAuditioningFirst = true);
	void swapClips(Clip* newClip, Clip* oldClip, int32_t clipIndex);
	Clip* replaceInstrumentClipWithAudioClip(Clip* oldClip, int32_t clipIndex);
	void setDefaultVelocityForAllInstruments(uint8_t newDefaultVelocity);
	void midiCableBendRangeUpdatedViaMessage(ModelStack* modelStack, MIDICable& cable, int32_t channelOrZone,
	                                         int32_t whichBendRange, int32_t bendSemitones);
	Error addInstrumentsToFileItems(OutputType outputType);

	uint32_t getQuarterNoteLength();
	uint32_t getBarLength();
	uint32_t getSixteenthNoteLength();
	ModelStackWithThreeMainThings* setupModelStackWithSongAsTimelineCounter(void* memory);
	ModelStackWithTimelineCounter* setupModelStackWithCurrentClip(void* memory);
	ModelStackWithThreeMainThings* addToModelStack(ModelStack* modelStack);
	/// Gets a modelstack with the song-global unpatched param paramID.
	/// used in performance view and in automation arranger view
	ModelStackWithAutoParam* getModelStackWithParam(ModelStackWithThreeMainThings* modelStack, int32_t paramID);

	// TimelineCounter implementation
	[[nodiscard]] int32_t getLastProcessedPos() const override;
	[[nodiscard]] uint32_t getLivePos() const override;
	[[nodiscard]] int32_t getLoopLength() const override;
	[[nodiscard]] bool isPlayingAutomationNow() const override;
	[[nodiscard]] bool backtrackingCouldLoopBackToEnd() const override;
	[[nodiscard]] int32_t getPosAtWhichPlaybackWillCut(ModelStackWithTimelineCounter const* modelStack) const override;
	void getActiveModControllable(ModelStackWithTimelineCounter* modelStack) override;
	void expectEvent() override;
	TimelineCounter* getTimelineCounterToRecordTo() override;

	// Reverb params to be stored here between loading and song being made the active one
	dsp::Reverb::Model model;
	float reverbRoomSize;
	float reverbHPF;
	float reverbLPF;
	float reverbDamp;
	float reverbWidth;
	int32_t reverbPan;
	int32_t reverbSidechainVolume;
	int32_t reverbSidechainShape;
	int32_t reverbSidechainAttack;
	int32_t reverbSidechainRelease;
	SyncLevel reverbSidechainSync;

	// START ~ new Automation Arranger View Variables
	auto& last_selected_param_id_for_session() { return navigation.active().automation.lastSelectedParamID; }
	const auto& last_selected_param_id_for_session() const {
		return navigation.active().automation.lastSelectedParamID;
	}
	auto& last_selected_param_kind_for_session() { return navigation.active().automation.lastSelectedParamKind; }
	const auto& last_selected_param_kind_for_session() const {
		return navigation.active().automation.lastSelectedParamKind;
	}
	auto& last_selected_param_shortcut_x_for_session() {
		return navigation.active().automation.lastSelectedParamShortcutX;
	}
	const auto& last_selected_param_shortcut_x_for_session() const {
		return navigation.active().automation.lastSelectedParamShortcutX;
	}
	auto& last_selected_param_shortcut_y_for_session() {
		return navigation.active().automation.lastSelectedParamShortcutY;
	}
	const auto& last_selected_param_shortcut_y_for_session() const {
		return navigation.active().automation.lastSelectedParamShortcutY;
	}
	auto& last_selected_param_array_position_for_session() {
		return navigation.active().automation.lastSelectedParamArrayPosition;
	}
	const auto& last_selected_param_array_position_for_session() const {
		return navigation.active().automation.lastSelectedParamArrayPosition;
	}

	// END ~ new Automation Arranger View Variables

	// Song level transpose control (encoder actions)
	void commandTranspose(int32_t interval);
	int32_t masterTransposeInterval = 0;
	void transpose(int32_t interval);
	void adjustMasterTransposeInterval(int32_t interval);
	void displayMasterTransposeInterval();

	// MIDI controlled song transpose
	bool hasBeenTransposed = 0;
	int16_t transposeOffset = 0;

	int32_t countAudioVoices() const;

	// Chord memory
	uint8_t chordMemNoteCount[kDisplayHeight] = {0};
	uint8_t chordMem[kDisplayHeight][MAX_NOTES_CHORD_MEM] = {0};

	// Tempo automation
	void clearTempoAutomation();
	void updateBPMFromAutomation();

	float calculateBPM() {
		float timePerTimerTick = getTimePerTimerTickFloat();
		return calculateBPM(timePerTimerTick);
	}
	float calculateBPM(float timePerTimerTick) {

		if (insideWorldTickMagnitude > 0) {
			timePerTimerTick *= ((uint32_t)1 << (insideWorldTickMagnitude));
		}
		float tempoBPM = (float)110250 / timePerTimerTick;
		if (insideWorldTickMagnitude < 0) {
			tempoBPM *= ((uint32_t)1 << (-insideWorldTickMagnitude));
		}
		return tempoBPM;
	}

	int8_t defaultAudioClipOverdubOutputCloning = -1; // -1 means no default set

	// Threshold
	void changeThresholdRecordingMode(int8_t offset);
	void displayThresholdRecordingMode();
	ThresholdRecordingMode thresholdRecordingMode;

private:
	ScaleMapper scaleMapper;
	NoteSet userScaleNotes;
	bool fillModeActive;
	SongClipSelection clip_selection;
	RetainedList<Output> undo_detached_outputs;
	SongNavigation navigation;
	void inputTickScalePotentiallyJustChanged(uint32_t oldScale);
	Error readClipsFromFile(Deserializer& reader, ClipArray* clipArray);
	void addInstrumentToHibernationList(Instrument* instrument);
	void deleteAllBackedUpParamManagers(bool shouldAlsoEmptyVector = true);
	void deleteAllBackedUpParamManagersWithClips();
	void deleteAllOutputs(Output** prevPointer);
	void setupClipIndexesForSaving();
	void setBPMInner(float tempoBPM, bool shouldLogAction);
	void clearTempoAutomation(float tempoBPM);
	int32_t intBPM{0};
};

extern Song* currentSong;
extern Song* preLoadedSong;
