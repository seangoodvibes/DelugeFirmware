import fs from "node:fs"
import path from "node:path"
import { fileURLToPath } from "node:url"

const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)
const repoRoot = path.resolve(__dirname, "../..")

const input = {
  menusCpp: path.join(repoRoot, "src/deluge/gui/ui/menus.cpp"),
  generatedMenus: path.join(
    repoRoot,
    "src/deluge/gui/menu_item/generate/g_menus.inc",
  ),
  clipSettings: path.join(
    repoRoot,
    "src/deluge/gui/context_menu/clip_settings/clip_settings.cpp",
  ),
  clipLaunchStyle: path.join(
    repoRoot,
    "src/deluge/gui/context_menu/clip_settings/launch_style.cpp",
  ),
  instrumentClipViewCpp: path.join(
    repoRoot,
    "src/deluge/gui/views/instrument_clip_view.cpp",
  ),
  syncHeader: path.join(repoRoot, "src/deluge/model/sync.h"),
  syncCpp: path.join(repoRoot, "src/deluge/model/sync.cpp"),
  utilFunctionsCpp: path.join(repoRoot, "src/deluge/util/functions.cpp"),
  flashStorageCpp: path.join(repoRoot, "src/deluge/storage/flash_storage.cpp"),
  definitionsCxx: path.join(repoRoot, "src/definitions_cxx.hpp"),
  lookupTablesCpp: path.join(
    repoRoot,
    "src/deluge/util/lookuptables/lookuptables.cpp",
  ),
  globalEffectableCpp: path.join(
    repoRoot,
    "src/deluge/model/global_effectable/global_effectable.cpp",
  ),
  runtimeFeatureMenuCpp: path.join(
    repoRoot,
    "src/deluge/gui/menu_item/runtime_feature/settings.cpp",
  ),
  runtimeFeatureSettingsCpp: path.join(
    repoRoot,
    "src/deluge/model/settings/runtime_feature_settings.cpp",
  ),
  menuItemRoot: path.join(repoRoot, "src/deluge/gui/menu_item"),
  contextMenuRoot: path.join(repoRoot, "src/deluge/gui/context_menu"),
  presetScalesHeader: path.join(
    repoRoot,
    "src/deluge/model/scale/preset_scales.h",
  ),
  english: path.join(repoRoot, "src/deluge/gui/l10n/english.json"),
  sevenSeg: path.join(repoRoot, "src/deluge/gui/l10n/seven_segment.json"),
  menuTags: path.join(repoRoot, "website/src/data/menu-hierarchy-tags.json"),
}

const outputPath = path.join(
  repoRoot,
  "website/src/data/generated/menu-hierarchies.json",
)

const MENU_TREES = {
  settingsMenu: "settingsRootMenu",
  songMenu: "soundEditorRootMenuSongView",
  performFxMenu: "soundEditorRootMenuPerformanceView",
  audioClipMenu: "soundEditorRootMenuAudioClip",
  soundMenu: "soundEditorRootMenu",
  kitFxMenu: "soundEditorRootMenuKitGlobalFX",
  midiInstrumentMenu: "soundEditorRootMenuMIDIOrCV",
  cvInstrumentMenu: "soundEditorRootMenuMIDIOrCV",
  noteEditorMenu: "noteEditorRootMenu",
  noteRowEditorMenu: "noteRowEditorRootMenu",
}

const VIRTUAL_TREES = {
  songClipSettingsMenu: "songClipSettingsMenu",
}

const MAINTENANCE_ANCHORS = {
  specialCaseVars: [
    "audioSourceSelectorMenu",
    "swingIntervalMenu",
    "defaultSwingIntervalMenu",
    "noteIteranceMenu",
    "noteRowIteranceMenu",
    "cvSelectionMenu",
    "gateSelectionMenu",
    "devicesMenu",
    "patchCablesMenu",
    "runtimeFeatureSettingsMenu",
    "arpPatternMenu",
  ],
  helperSelectorVars: ["audioInputSelector", "midiDeviceMenu"],
}

const ROOT_MENU_TREE_IGNORE = new Set([
  // Internal or context-specific roots we intentionally do not render
  // as top-level docs sections.
  "noteCustomIteranceRootMenu",
  "noteRowCustomIteranceRootMenu",
  "soundEditorRootMenuDrum",
  "soundEditorRootMenuGateDrum",
  "soundEditorRootMenuMidiDrum",
])

function validateInputPaths(inputPaths) {
  const missing = []

  for (const [key, filePath] of Object.entries(inputPaths)) {
    if (typeof filePath !== "string" || !filePath.trim()) {
      missing.push({ key, filePath: String(filePath), reason: "empty path" })
      continue
    }

    if (!fs.existsSync(filePath)) {
      missing.push({ key, filePath, reason: "not found" })
      continue
    }

    if (
      (key.endsWith("Root") || key.endsWith("root")) &&
      !fs.statSync(filePath).isDirectory()
    ) {
      missing.push({ key, filePath, reason: "expected directory" })
      continue
    }
  }

  if (missing.length === 0) {
    return
  }

  const detailLines = missing.map(
    ({ key, filePath, reason }) =>
      `  - ${key}: ${path.relative(repoRoot, filePath)} (${reason})`,
  )

  throw new Error(
    [
      "Menu hierarchy generation failed: required input path(s) are invalid.",
      ...detailLines,
      "Update the input path mapping in website/scripts/generate-menu-hierarchies.mjs.",
    ].join("\n"),
  )
}

function discoverFirmwareRootMenuVars(menusCpp) {
  const rootVars = new Set()
  const declarationPattern =
    /^\s*(?:PLACE_SDRAM_BSS\s+|PLACE_SDRAM_DATA\s+)?[A-Za-z_][A-Za-z0-9_:\s<>,*&]*?\s+([A-Za-z_][A-Za-z0-9_:]*)\s*\{\s*(?:l10n::String::)?STRING_FOR_[A-Z0-9_]+/gm

  for (const match of menusCpp.matchAll(declarationPattern)) {
    const varName = stripNamespace(match[1])
    if (
      /(?:^|[A-Za-z])RootMenu(?:[A-Za-z0-9_]*)?$/.test(varName) ||
      varName === "settingsRootMenu" ||
      varName === "noteEditorRootMenu" ||
      varName === "noteRowEditorRootMenu"
    ) {
      rootVars.add(varName)
    }
  }

  return [...rootVars].sort()
}

function ensureAnchorsExist(varToToken, combinedSource, menusCpp) {
  const missingRootVars = Object.values(MENU_TREES).filter(
    (varName) => !varToToken.has(varName),
  )

  if (missingRootVars.length > 0) {
    throw new Error(
      [
        "Menu hierarchy generation failed: one or more root menu vars were not found.",
        `Missing roots: ${missingRootVars.join(", ")}`,
        "Update MENU_TREES in website/scripts/generate-menu-hierarchies.mjs if firmware vars were renamed.",
      ].join("\n"),
    )
  }

  const missingSpecialCaseVars = MAINTENANCE_ANCHORS.specialCaseVars.filter(
    (varName) => !varToToken.has(varName),
  )
  if (missingSpecialCaseVars.length > 0) {
    throw new Error(
      [
        "Menu hierarchy generation failed: special-case menu vars were not found.",
        `Missing special-case vars: ${missingSpecialCaseVars.join(", ")}`,
        "Update buildDynamicSelectionChildren / buildNode special-case handling in website/scripts/generate-menu-hierarchies.mjs to match renamed firmware vars.",
      ].join("\n"),
    )
  }

  const missingHelperSelectorVars =
    MAINTENANCE_ANCHORS.helperSelectorVars.filter(
      (varName) =>
        !new RegExp(`\\b${escapeForRegex(varName)}\\b`).test(combinedSource),
    )
  if (missingHelperSelectorVars.length > 0) {
    throw new Error(
      [
        "Menu hierarchy generation failed: helper selector vars were not found in firmware source.",
        `Missing helper vars: ${missingHelperSelectorVars.join(", ")}`,
        "Update selector helper mappings in website/scripts/generate-menu-hierarchies.mjs if firmware internals were renamed.",
      ].join("\n"),
    )
  }

  const discoveredRoots = discoverFirmwareRootMenuVars(menusCpp)
  const trackedRoots = new Set(Object.values(MENU_TREES))
  const untrackedRoots = discoveredRoots.filter(
    (varName) =>
      !trackedRoots.has(varName) && !ROOT_MENU_TREE_IGNORE.has(varName),
  )

  if (untrackedRoots.length > 0) {
    throw new Error(
      [
        "Menu hierarchy generation failed: new firmware root menu vars were detected but are not mapped.",
        `Untracked roots: ${untrackedRoots.join(", ")}`,
        "Add each new root to MENU_TREES (for documented trees) or ROOT_MENU_TREE_IGNORE (for intentionally undocumented/internal roots) in website/scripts/generate-menu-hierarchies.mjs.",
      ].join("\n"),
    )
  }
}

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, "utf8"))
}

function readMenuTags(filePath) {
  const parsed = readJson(filePath)
  const tagsByVarName = new Map()

  if (!parsed || typeof parsed !== "object") {
    return tagsByVarName
  }

  for (const [varName, value] of Object.entries(parsed)) {
    if (typeof varName !== "string" || !varName.trim()) {
      continue
    }

    const tags = Array.isArray(value) ? value : [value]
    const normalizedTags = tags
      .filter((tag) => typeof tag === "string")
      .map((tag) => tag.trim())
      .filter(Boolean)

    if (normalizedTags.length > 0) {
      tagsByVarName.set(varName, normalizedTags[0])
    }
  }

  return tagsByVarName
}

function applyMenuTags(node, tagsByVarName) {
  if (!node || typeof node !== "object") {
    return node
  }

  const tag = tagsByVarName.get(node.varName)
  if (tag) {
    node.tag = tag
  }

  for (const child of node.children ?? []) {
    applyMenuTags(child, tagsByVarName)
  }

  return node
}

function applyDerivedMenuTags(node) {
  if (!node || typeof node !== "object") {
    return node
  }

  if (
    !node.tag &&
    /(?:tplts|dtted)/i.test(`${node.oled ?? ""} ${node.code ?? ""}`)
  ) {
    node.tag = "c1.0"
  }

  for (const child of node.children ?? []) {
    applyDerivedMenuTags(child)
  }

  return node
}

function stripNamespace(symbol) {
  return symbol.split("::").at(-1)
}

function unique(values) {
  return [...new Set(values)]
}

function listFilesRecursive(rootDir, extensions) {
  const results = []
  const stack = [rootDir]

  while (stack.length > 0) {
    const current = stack.pop()
    const entries = fs.readdirSync(current, { withFileTypes: true })
    for (const entry of entries) {
      const fullPath = path.join(current, entry.name)
      if (entry.isDirectory()) {
        stack.push(fullPath)
      } else if (extensions.some((ext) => entry.name.endsWith(ext))) {
        results.push(fullPath)
      }
    }
  }

  return results
}

function collectVarToToken(sourceText) {
  const map = new Map()
  const declarationPattern =
    /^\s*(?:PLACE_SDRAM_BSS\s+|PLACE_SDRAM_DATA\s+)?[A-Za-z_][A-Za-z0-9_:\s<>,*&]*?\s+([A-Za-z_][A-Za-z0-9_:]*)\s*\{\s*(?:l10n::String::)?(STRING_FOR_[A-Z0-9_]+)/gm

  for (const match of sourceText.matchAll(declarationPattern)) {
    map.set(stripNamespace(match[1]), match[2])
  }

  return map
}

function normalizeTypeName(typeSpec) {
  const cleaned = typeSpec.replace(/\b(const|volatile|static|extern)\b/g, "")
  const token = cleaned.trim().split(/\s+/).at(-1) ?? ""
  return token.replace(/[&*]+$/g, "")
}

function collectVarToType(sourceText) {
  const map = new Map()
  const declarationPattern =
    /^\s*(?:PLACE_SDRAM_BSS\s+|PLACE_SDRAM_DATA\s+)?([A-Za-z_][A-Za-z0-9_:\s<>,*&]*?)\s+([A-Za-z_][A-Za-z0-9_:]*)\s*\{\s*(?:l10n::String::)?STRING_FOR_[A-Z0-9_]+/gm

  for (const match of sourceText.matchAll(declarationPattern)) {
    const typeName = normalizeTypeName(match[1])
    map.set(stripNamespace(match[2]), typeName)
  }

  return map
}

function extractPresetScaleNames(sourceText) {
  const names = []
  for (const match of sourceText.matchAll(
    /DEF\(\s*[A-Z0-9_]+\s*,\s*"([^"]+)"\s*,/g,
  )) {
    names.push(match[1])
  }
  return unique(names)
}

function extractFillOptionLabels(sourceText) {
  const signatureMatch =
    /const\s+char\*\s+InstrumentClipView::getFillString\s*\([^)]*\)/m.exec(
      sourceText,
    )
  if (!signatureMatch || signatureMatch.index === undefined) {
    return []
  }

  const openBraceIndex = sourceText.indexOf("{", signatureMatch.index)
  if (openBraceIndex < 0) {
    return []
  }

  const closeBraceIndex = findMatchingBrace(sourceText, openBraceIndex)
  if (closeBraceIndex < 0) {
    return []
  }

  const body = sourceText.slice(openBraceIndex + 1, closeBraceIndex)
  const labels = []
  for (const match of body.matchAll(/return\s+"([^"]+)"\s*;/g)) {
    labels.push(match[1])
  }

  return unique(labels)
}

function extractNumericDefines(sourceText) {
  const defines = new Map()
  for (const match of sourceText.matchAll(
    /^\s*#define\s+([A-Z_][A-Z0-9_]*)\s+(-?\d+)\s*$/gm,
  )) {
    defines.set(match[1], Number.parseInt(match[2], 10))
  }
  return defines
}

function evaluateIntegerExpression(expression, valueMap) {
  if (!expression) {
    return null
  }

  let expanded = expression
  for (const [name, value] of valueMap) {
    expanded = expanded.replace(
      new RegExp(`\\b${escapeForRegex(name)}\\b`, "g"),
      String(value),
    )
  }

  if (!/^[0-9+\-\s()]+$/.test(expanded)) {
    return null
  }

  try {
    const value = Function(`"use strict"; return (${expanded});`)()
    return Number.isInteger(value) ? value : null
  } catch {
    return null
  }
}

function extractConstexprIntegers(sourceText) {
  const values = new Map()
  const pending = []

  for (const match of sourceText.matchAll(
    /constexpr\s+int32_t\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([^;]+);/g,
  )) {
    pending.push({ name: match[1], expression: match[2] })
  }

  let progressed = true
  while (progressed && pending.length > 0) {
    progressed = false
    for (let i = pending.length - 1; i >= 0; i -= 1) {
      const candidate = pending[i]
      const value = evaluateIntegerExpression(candidate.expression, values)
      if (value === null) {
        continue
      }
      values.set(candidate.name, value)
      pending.splice(i, 1)
      progressed = true
    }
  }

  return values
}

function extractIterancePresets(sourceText) {
  const tableMatch = /iterancePresets\s*=\s*\{([\s\S]*?)\}\s*;/m.exec(
    sourceText,
  )
  if (!tableMatch) {
    return []
  }

  const presets = []
  for (const match of tableMatch[1].matchAll(
    /Iterance\s*\{\s*(\d+)\s*,\s*(0b[01]+|\d+)\s*\}/g,
  )) {
    presets.push({
      divisor: Number.parseInt(match[1], 10),
      stepMask: Number.parseInt(match[2].replace(/^0b/, ""), 2),
    })
  }
  return presets
}

function extractIteranceDisplayLiterals(sourceText) {
  const signatureMatch =
    /void\s+InstrumentClipView::displayIterance\s*\([^)]*\)/m.exec(sourceText)
  if (!signatureMatch || signatureMatch.index === undefined) {
    return null
  }

  const openBraceIndex = sourceText.indexOf("{", signatureMatch.index)
  if (openBraceIndex < 0) {
    return null
  }

  const closeBraceIndex = findMatchingBrace(sourceText, openBraceIndex)
  if (closeBraceIndex < 0) {
    return null
  }

  const body = sourceText.slice(openBraceIndex + 1, closeBraceIndex)
  const branchLabels = [
    ...body.matchAll(
      /(?:if|else\s+if)\s*\(([^)]+)\)\s*\{[\s\S]*?strcpy\(buffer,\s*display->haveOLED\(\)\s*\?\s*"[^"]*"\s*:\s*"([^"]+)"\)/g,
    ),
  ].map((match) => ({
    condition: match[1].trim(),
    shortLabel: match[2],
  }))

  const formatMatch =
    /sprintf\(buffer,\s*display->haveOLED\(\)\s*\?\s*"([^"]+)"\s*:\s*"([^"]+)"\s*,/m.exec(
      body,
    )

  return {
    branchLabels,
    oledFormat: formatMatch ? formatMatch[1] : null,
    codeFormat: formatMatch ? formatMatch[2] : null,
  }
}

function resolveIterancePresetIndexFromCondition(condition, valueMap) {
  const equalityMatch = /iterancePreset\s*==\s*([^&|]+)/.exec(condition)
  if (!equalityMatch) {
    return null
  }

  return evaluateIntegerExpression(equalityMatch[1].trim(), valueMap)
}

function formatIteranceLabel(formatString, step, divisor) {
  if (!formatString) {
    return null
  }

  let replaced = 0
  const result = formatString.replace(/%d/g, () => {
    const value = replaced === 0 ? step : divisor
    replaced += 1
    return String(value)
  })

  return replaced >= 2 ? result : null
}

function highestSetBitIndex(mask, maxIndex) {
  for (let i = maxIndex; i >= 0; i -= 1) {
    if ((mask & (1 << i)) !== 0) {
      return i
    }
  }
  return -1
}

function buildIteranceOptionLabels(
  iterancePresets,
  iteranceLiterals,
  iteranceConstants,
) {
  const numPresets = iterancePresets.length
  const defaultPreset = 0
  const customPreset = numPresets + 1
  const presetLabelMap = new Map()

  const conditionValues = new Map(iteranceConstants)
  conditionValues.set("DEFAULT_PRESET_INDEX", defaultPreset)
  conditionValues.set("CUSTOM_PRESET_INDEX", customPreset)

  if (
    iterancePresets.length === 0 ||
    !iteranceLiterals ||
    !iteranceLiterals.branchLabels ||
    iteranceLiterals.branchLabels.length === 0 ||
    !iteranceLiterals.oledFormat ||
    !iteranceLiterals.codeFormat
  ) {
    return { labels: [], customPreset }
  }

  for (const { condition, shortLabel } of iteranceLiterals.branchLabels) {
    const presetIndex = resolveIterancePresetIndexFromCondition(
      condition,
      conditionValues,
    )
    if (presetIndex === null) {
      continue
    }
    presetLabelMap.set(presetIndex, {
      oled: shortLabel,
      code: shortLabel,
    })
  }

  const labels = []
  for (
    let presetIndex = defaultPreset;
    presetIndex <= customPreset;
    presetIndex += 1
  ) {
    const specialLabel = presetLabelMap.get(presetIndex)
    if (specialLabel) {
      labels.push(specialLabel)
      continue
    }

    const preset = iterancePresets[presetIndex - 1]
    if (!preset || preset.divisor <= 0) {
      continue
    }

    const activeStep = highestSetBitIndex(preset.stepMask, preset.divisor)
    if (activeStep < 0) {
      continue
    }

    const step = activeStep + 1
    const oledLabel = formatIteranceLabel(
      iteranceLiterals.oledFormat,
      step,
      preset.divisor,
    )
    const codeLabel = formatIteranceLabel(
      iteranceLiterals.codeFormat,
      step,
      preset.divisor,
    )
    if (!oledLabel || !codeLabel) {
      continue
    }

    labels.push({
      oled: oledLabel,
      code: codeLabel,
    })
  }

  return { labels, customPreset }
}

function extractDefaultMagnitude(sourceText) {
  const directMatch =
    /defaultMagnitude\s*=\s*(-?\d+)\s*;[\s\S]*?defaultSwingInterval\s*=\s*8\s*-\s*defaultMagnitude/m.exec(
      sourceText,
    )
  return directMatch ? Number.parseInt(directMatch[1], 10) : null
}

function extractNoteMagnitudeBase(sourceText) {
  const match =
    /getNoteMagnitudeFfromNoteLength\s*\([^)]*\)\s*\{[\s\S]*?noteMagnitude\s*=\s*(-?\d+)\s*-\s*tickMagnitude\s*;/m.exec(
      sourceText,
    )
  return match ? Number.parseInt(match[1], 10) : null
}

function extractSyncTypeSuffixes(sourceText) {
  const suffixes = new Map()
  for (const match of sourceText.matchAll(
    /case\s+([A-Z_][A-Z0-9_]*)\s*:\s*([\s\S]*?)break\s*;/g,
  )) {
    const typeName = match[1]
    const caseBody = match[2]
    const suffixMatch = /typeStr\s*=\s*"([^"]+)"\s*;/.exec(caseBody)
    suffixes.set(typeName, suffixMatch ? suffixMatch[1] : "")
  }
  return suffixes
}

function extractEnumEntries(sourceText, enumName) {
  const enumMatch = new RegExp(
    `enum\\s+${escapeForRegex(enumName)}\\s*:\\s*\\w+\\s*\\{([\\s\\S]*?)\\}`,
    "m",
  ).exec(sourceText)
  if (!enumMatch) {
    return []
  }

  const entries = []
  for (const match of enumMatch[1].matchAll(
    /\b([A-Z_][A-Z0-9_]*)\s*=\s*(-?\d+)/g,
  )) {
    entries.push({
      name: match[1],
      value: Number.parseInt(match[2], 10),
    })
  }

  return entries.sort((a, b) => a.value - b.value)
}

function findConstantInExpression(sourceText, expressionPattern) {
  const match = expressionPattern.exec(sourceText)
  return match ? match[1] : null
}

function getSyncTypeEntryForOption(option, syncTypeEntries) {
  let resolved = null
  for (const entry of syncTypeEntries) {
    if (option >= entry.value) {
      resolved = entry
    } else {
      break
    }
  }
  return resolved
}

function getSyncTypeGroupIndex(syncTypeEntries, typeName) {
  return syncTypeEntries.findIndex((entry) => entry.name === typeName)
}

function syncLevelForOption(option, syncTypeEntries, syncTypeName) {
  const groupIndex = getSyncTypeGroupIndex(syncTypeEntries, syncTypeName)
  if (groupIndex < 0) {
    return option
  }

  const groupStart = syncTypeEntries[groupIndex]?.value ?? 0
  const offset = groupIndex > 0 ? 1 : 0
  return option - groupStart + offset
}

function buildSyncOptionContext(
  syncHeader,
  syncCpp,
  utilFunctionsCpp,
  flashStorageCpp,
) {
  const syncTypeEntries = extractEnumEntries(syncHeader, "SyncType")
  const syncLevelEntries = extractEnumEntries(syncHeader, "SyncLevel")
  if (syncTypeEntries.length === 0 || syncLevelEntries.length === 0) {
    return null
  }

  const syncLevelValues = new Map(
    syncLevelEntries.map((entry) => [entry.name, entry.value]),
  )
  const shiftAnchorName = findConstantInExpression(
    syncCpp,
    /shift\s*=\s*([A-Z_][A-Z0-9_]*)\s*-\s*level/,
  )
  const barLevelName = findConstantInExpression(
    syncCpp,
    /magnitudeLevelBars\s*=\s*([A-Z_][A-Z0-9_]*)\s*-\s*tickMagnitude/,
  )
  if (!shiftAnchorName || !barLevelName) {
    return null
  }

  const shiftAnchor = syncLevelValues.get(shiftAnchorName)
  const barLevelBase = syncLevelValues.get(barLevelName)
  const defaultMagnitude = extractDefaultMagnitude(flashStorageCpp)
  const noteMagnitudeBase = extractNoteMagnitudeBase(utilFunctionsCpp)
  if (
    shiftAnchor === undefined ||
    barLevelBase === undefined ||
    defaultMagnitude === null ||
    noteMagnitudeBase === null
  ) {
    return null
  }

  return {
    syncTypeEntries,
    syncTypeSuffixes: extractSyncTypeSuffixes(syncCpp),
    shiftAnchor,
    barLevelBase,
    defaultMagnitude,
    noteMagnitudeBase,
  }
}

function getNoteMagnitudeFromSyncLevel(
  syncLevel,
  syncLevel256th,
  defaultMagnitude,
  noteMagnitudeBase,
) {
  const noteLength = 3 << (syncLevel256th - syncLevel)

  let noteMagnitude = noteMagnitudeBase - defaultMagnitude
  let level = 3
  while (level < noteLength) {
    noteMagnitude += 1
    level <<= 1
  }

  return noteMagnitude
}

function buildOledSyncLabel(noteMagnitude, suffix, appendSuffixForBars) {
  if (noteMagnitude < 0) {
    const division = 1 << -noteMagnitude
    const ordinal = division % 10 === 2 ? "nd" : "th"
    return `${division}${ordinal}${suffix ?? ""}`
  }

  const bars = 1 << noteMagnitude
  return appendSuffixForBars && suffix ? `${bars}-bar${suffix}` : `${bars}-bar`
}

function buildSevenSegSyncLabel(noteMagnitude, suffix) {
  const upperSuffix = (suffix ?? "").toUpperCase()

  if (noteMagnitude < 0) {
    const division = 1 << -noteMagnitude
    let base = ""
    if (division <= 9999) {
      base = `${division}`
      if (division === 2 || division === 32) {
        base += "ND"
      } else if (division <= 99) {
        base += "TH"
      } else if (division <= 999) {
        base += "T"
      }
    } else {
      base = "TINY"
    }

    return `${base}${upperSuffix}`
  }

  const bars = 1 << noteMagnitude
  let base = ""
  if (bars <= 9999) {
    base = `${bars}`
    if (base.length === 1) {
      base += "BAR"
    } else if (base.length <= 3) {
      base += "B"
    }
  } else {
    base = "BIG"
  }

  return `${base}${upperSuffix}`
}

function buildSyncLabelForOption(option, syncContext) {
  const syncTypeEntry = getSyncTypeEntryForOption(
    option,
    syncContext.syncTypeEntries,
  )
  if (!syncTypeEntry) {
    return null
  }

  const syncType = syncTypeEntry.name
  const syncTypeGroupIndex = getSyncTypeGroupIndex(
    syncContext.syncTypeEntries,
    syncType,
  )
  const syncLevel = syncLevelForOption(
    option,
    syncContext.syncTypeEntries,
    syncType,
  )
  const suffix = syncContext.syncTypeSuffixes.get(syncType) ?? ""

  const noteMagnitude = getNoteMagnitudeFromSyncLevel(
    syncLevel,
    syncContext.shiftAnchor,
    syncContext.defaultMagnitude,
    syncContext.noteMagnitudeBase,
  )

  const magnitudeLevelBars =
    syncContext.barLevelBase - syncContext.defaultMagnitude
  const appendSuffixForBars =
    syncTypeGroupIndex > 0 && syncLevel <= magnitudeLevelBars

  return {
    oled: buildOledSyncLabel(noteMagnitude, suffix, appendSuffixForBars),
    code: buildSevenSegSyncLabel(noteMagnitude, suffix),
  }
}

function extractInitializerBody(sourceText, varName) {
  const markerPattern = new RegExp(`\\b${varName}\\s*\\{`, "m")
  const markerMatch = markerPattern.exec(sourceText)
  if (!markerMatch || markerMatch.index === undefined) {
    return undefined
  }

  const openBraceIndex = sourceText.indexOf("{", markerMatch.index)
  if (openBraceIndex < 0) {
    return undefined
  }

  let depth = 0
  for (let i = openBraceIndex; i < sourceText.length; i++) {
    const c = sourceText[i]
    if (c === "{") {
      depth += 1
    } else if (c === "}") {
      depth -= 1
      if (depth === 0) {
        return sourceText.slice(openBraceIndex + 1, i)
      }
    }
  }

  return undefined
}

function extractArrayChildren(sourceText) {
  const map = new Map()
  const pattern =
    /std::array<MenuItem\*,\s*\d+>\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*\{([\s\S]*?)\};/g

  for (const match of sourceText.matchAll(pattern)) {
    const arrayName = match[1]
    const refs = []
    for (const refMatch of match[2].matchAll(
      /&\s*([A-Za-z_][A-Za-z0-9_:]*)/g,
    )) {
      refs.push(stripNamespace(refMatch[1]))
    }
    map.set(arrayName, unique(refs))
  }

  return map
}

function extractChildren(sourceText, arrayChildren, varName) {
  const body = extractInitializerBody(sourceText, varName)
  if (!body) {
    return []
  }

  const refs = []
  for (const match of body.matchAll(/&\s*([A-Za-z_][A-Za-z0-9_:]*)/g)) {
    refs.push(stripNamespace(match[1]))
  }

  for (const [arrayName, members] of arrayChildren) {
    const arrayPattern = new RegExp(`\\b${arrayName}\\b`)
    if (arrayPattern.test(body)) {
      refs.push(...members)
    }
  }

  return unique(refs)
}

function extractRuntimeFeatureSubMenuOrder(sourceText) {
  const match = /subMenuEntries\s*\{([\s\S]*?)\}\s*;/.exec(sourceText)
  if (!match) {
    return []
  }

  const refs = []
  for (const refMatch of match[1].matchAll(/&\s*([A-Za-z_][A-Za-z0-9_:]*)/g)) {
    refs.push(stripNamespace(refMatch[1]))
  }
  return refs
}

function extractRuntimeFeatureTypeFromClassCtor(className, sourceText) {
  const pattern = new RegExp(
    `${escapeForRegex(className)}\\s*\\([^)]*\\)\\s*:\\s*[^{;]*RuntimeFeatureSettingType::([A-Za-z0-9_]+)`,
  )
  const match = pattern.exec(sourceText)
  return match ? match[1] : null
}

function extractRuntimeFeatureMenuVarToSettingType(menuSource, menuItemCorpus) {
  const mapping = new Map()

  for (const match of menuSource.matchAll(
    /\b[A-Za-z_][A-Za-z0-9_:<>]*\s+(menu[A-Za-z0-9_]+)\(RuntimeFeatureSettingType::([A-Za-z0-9_]+)\)\s*;/g,
  )) {
    mapping.set(match[1], match[2])
  }

  for (const match of menuSource.matchAll(
    /\b([A-Za-z_][A-Za-z0-9_:<>]*)\s+(menu[A-Za-z0-9_]+)\s*\{\s*\}\s*;/g,
  )) {
    const className = stripNamespace(match[1])
    const varName = match[2]
    if (mapping.has(varName)) {
      continue
    }

    const settingType = extractRuntimeFeatureTypeFromClassCtor(
      className,
      menuItemCorpus,
    )
    if (settingType) {
      mapping.set(varName, settingType)
    }
  }

  return mapping
}

function extractRuntimeFeatureSettingTypeToToken(sourceText) {
  const mapping = new Map()

  for (const match of sourceText.matchAll(
    /Setup(?:OnOff|SyncScalingAction|EmulatedDisplay)Setting\(\s*settings\[RuntimeFeatureSettingType::([A-Za-z0-9_]+)\]\s*,\s*(STRING_FOR_[A-Z0-9_]+)/g,
  )) {
    mapping.set(match[1], match[2])
  }

  return mapping
}

function buildRuntimeFeatureChildren(english, sevenSeg, sourceStructure) {
  const children = []

  for (const varName of sourceStructure.runtimeFeatureMenuOrder) {
    const settingType =
      sourceStructure.runtimeFeatureMenuVarToSettingType.get(varName)
    if (!settingType) {
      continue
    }

    const token =
      sourceStructure.runtimeFeatureSettingTypeToToken.get(settingType)
    if (!token) {
      continue
    }

    const node = makeVirtualNode(english, sevenSeg, token, varName, [])
    if (node) {
      children.push(node)
    }
  }

  return children
}

function build() {
  validateInputPaths(input)

  const menusCpp = fs.readFileSync(input.menusCpp, "utf8")
  const generatedMenus = fs.readFileSync(input.generatedMenus, "utf8")
  const clipSettingsCpp = fs.readFileSync(input.clipSettings, "utf8")
  const clipLaunchStyleCpp = fs.readFileSync(input.clipLaunchStyle, "utf8")
  const instrumentClipViewCpp = fs.readFileSync(
    input.instrumentClipViewCpp,
    "utf8",
  )
  const syncHeader = fs.readFileSync(input.syncHeader, "utf8")
  const syncCpp = fs.readFileSync(input.syncCpp, "utf8")
  const utilFunctionsCpp = fs.readFileSync(input.utilFunctionsCpp, "utf8")
  const flashStorageCpp = fs.readFileSync(input.flashStorageCpp, "utf8")
  const definitionsCxx = fs.readFileSync(input.definitionsCxx, "utf8")
  const lookupTablesCpp = fs.readFileSync(input.lookupTablesCpp, "utf8")
  const runtimeFeatureMenuCpp = fs.readFileSync(
    input.runtimeFeatureMenuCpp,
    "utf8",
  )
  const runtimeFeatureSettingsCpp = fs.readFileSync(
    input.runtimeFeatureSettingsCpp,
    "utf8",
  )
  const presetScalesHeader = fs.readFileSync(input.presetScalesHeader, "utf8")
  const menuItemFiles = listFilesRecursive(input.menuItemRoot, [
    ".h",
    ".hpp",
    ".cpp",
  ])
  const contextMenuFiles = listFilesRecursive(input.contextMenuRoot, [
    ".h",
    ".hpp",
    ".cpp",
  ])
  const menuSourceFiles = unique([...menuItemFiles, ...contextMenuFiles])
  const menuItemCorpus = menuSourceFiles
    .map((filePath) => fs.readFileSync(filePath, "utf8"))
    .join("\n\n")
  const english = readJson(input.english).strings
  const sevenSeg = readJson(input.sevenSeg).strings
  const menuTagsByVarName = readMenuTags(input.menuTags)

  const combined = `${generatedMenus}\n${menusCpp}`
  const varToToken = new Map([
    ...collectVarToToken(generatedMenus),
    ...collectVarToToken(menusCpp),
    ...collectVarToToken(menuItemCorpus),
  ])
  const varToType = new Map([
    ...collectVarToType(generatedMenus),
    ...collectVarToType(menusCpp),
    ...collectVarToType(menuItemCorpus),
  ])
  const arrayChildren = extractArrayChildren(combined)
  const syncOptionContext = buildSyncOptionContext(
    syncHeader,
    syncCpp,
    utilFunctionsCpp,
    flashStorageCpp,
  )
  const iteranceConstants = extractConstexprIntegers(definitionsCxx)
  const iterancePresets = extractIterancePresets(lookupTablesCpp)
  const globalEffectableCpp = fs.readFileSync(input.globalEffectableCpp, "utf8")
  const iteranceLiterals = extractIteranceDisplayLiterals(instrumentClipViewCpp)
  const iteranceOptionData = buildIteranceOptionLabels(
    iterancePresets,
    iteranceLiterals,
    iteranceConstants,
  )
  const numericDefines = new Map([
    ...extractNumericDefines(syncHeader),
    ...extractNumericDefines(syncCpp),
  ])
  const syncOffLabel = labelFromToken(english, sevenSeg, "STRING_FOR_OFF")
  const sourceStructure = {
    combined,
    arrayChildren,
    varToToken,
    varToType,
    english,
    sevenSeg,
    menuItemCorpus,
    globalEffectableCpp,
    numericDefines,
    typeInheritanceCache: new Map(),
    presetScaleNames: extractPresetScaleNames(presetScalesHeader),
    fillOptionLabels: extractFillOptionLabels(instrumentClipViewCpp),
    iteranceOptionLabels: iteranceOptionData.labels,
    iteranceCustomPreset: iteranceOptionData.customPreset,
    syncOptionContext,
    syncOffLabel,
    runtimeFeatureMenuOrder: extractRuntimeFeatureSubMenuOrder(
      runtimeFeatureMenuCpp,
    ),
    runtimeFeatureMenuVarToSettingType:
      extractRuntimeFeatureMenuVarToSettingType(
        runtimeFeatureMenuCpp,
        menuItemCorpus,
      ),
    runtimeFeatureSettingTypeToToken: extractRuntimeFeatureSettingTypeToToken(
      runtimeFeatureSettingsCpp,
    ),
  }

  ensureAnchorsExist(varToToken, `${combined}\n${menuItemCorpus}`, menusCpp)

  const trees = {}
  for (const [treeKey, rootVar] of Object.entries(MENU_TREES)) {
    const node = buildNode(
      combined,
      arrayChildren,
      varToToken,
      english,
      sevenSeg,
      sourceStructure,
      rootVar,
      [],
      0,
    )
    if (node) {
      trees[treeKey] = applyDerivedMenuTags(
        applyMenuTags(node, menuTagsByVarName),
      )
    }
  }

  trees.songClipSettingsMenu = applyMenuTags(
    buildSongClipSettingsVirtualTree(
      clipSettingsCpp,
      clipLaunchStyleCpp,
      english,
      sevenSeg,
    ),
    menuTagsByVarName,
  )

  const payload = {
    generatedAt: new Date().toISOString(),
    sourceFiles: {
      menusCpp: path.relative(repoRoot, input.menusCpp),
      generatedMenus: path.relative(repoRoot, input.generatedMenus),
      clipSettings: path.relative(repoRoot, input.clipSettings),
      clipLaunchStyle: path.relative(repoRoot, input.clipLaunchStyle),
      instrumentClipViewCpp: path.relative(
        repoRoot,
        input.instrumentClipViewCpp,
      ),
      syncHeader: path.relative(repoRoot, input.syncHeader),
      syncCpp: path.relative(repoRoot, input.syncCpp),
      utilFunctionsCpp: path.relative(repoRoot, input.utilFunctionsCpp),
      flashStorageCpp: path.relative(repoRoot, input.flashStorageCpp),
      definitionsCxx: path.relative(repoRoot, input.definitionsCxx),
      lookupTablesCpp: path.relative(repoRoot, input.lookupTablesCpp),
      runtimeFeatureMenuCpp: path.relative(
        repoRoot,
        input.runtimeFeatureMenuCpp,
      ),
      runtimeFeatureSettingsCpp: path.relative(
        repoRoot,
        input.runtimeFeatureSettingsCpp,
      ),
      menuItemRoot: path.relative(repoRoot, input.menuItemRoot),
      presetScalesHeader: path.relative(repoRoot, input.presetScalesHeader),
      english: path.relative(repoRoot, input.english),
      sevenSeg: path.relative(repoRoot, input.sevenSeg),
      menuTags: path.relative(repoRoot, input.menuTags),
    },
    trees,
    mapping: {
      ...MENU_TREES,
      ...VIRTUAL_TREES,
    },
  }

  fs.mkdirSync(path.dirname(outputPath), { recursive: true })
  fs.writeFileSync(outputPath, `${JSON.stringify(payload, null, 2)}\n`, "utf8")
  console.log(
    `Generated ${Object.keys(trees).length} menu trees from firmware source into ${path.relative(repoRoot, outputPath)}.`,
  )
}

function labelFromToken(english, sevenSeg, token) {
  const oled = english[token]
  if (!oled) {
    return null
  }

  return {
    token,
    oled,
    code: sevenSeg[token] ?? oled,
  }
}

function applyVarNameLabelOverrides(varName, label) {
  const replaceWildcardWithNumber = (numberString) => ({
    ...label,
    oled: label.oled.replace("*", numberString),
    code: label.code.replace("*", numberString),
  })

  const envelopeMatch = /^env(\d+)Menu$/.exec(varName)
  if (envelopeMatch) {
    return replaceWildcardWithNumber(envelopeMatch[1])
  }

  const sourceMatch = /^source(\d+)(?:Menu|VolumeMenu)$/.exec(varName)
  if (sourceMatch && label.oled.includes("*")) {
    return replaceWildcardWithNumber(
      String(Number.parseInt(sourceMatch[1], 10) + 1),
    )
  }

  const modulatorMatch = /^modulator(\d+)Volume$/.exec(varName)
  if (modulatorMatch && label.oled.includes("*")) {
    return replaceWildcardWithNumber(
      String(Number.parseInt(modulatorMatch[1], 10) + 1),
    )
  }

  const midiTrackMatch = /^midiFollowChannelTrack(\d+)Menu$/.exec(varName)
  if (midiTrackMatch) {
    const index = Number.parseInt(midiTrackMatch[1], 10)
    if (Number.isInteger(index) && index >= 1 && index <= 16) {
      const suffix = String(index).padStart(2, "0")
      return {
        ...label,
        oled: `Channel Track${suffix}`,
        code: `TR${suffix}`,
      }
    }
  }

  return label
}

function makeVirtualNode(
  english,
  sevenSeg,
  token,
  varName,
  children = [],
  codeToken = token,
) {
  const label = labelFromToken(english, sevenSeg, token)
  if (!label) {
    return null
  }

  const code = sevenSeg[codeToken] ?? label.code

  return {
    varName,
    token: label.token,
    oled: label.oled,
    code,
    children,
  }
}

function cloneChildren(children) {
  return JSON.parse(JSON.stringify(children))
}

function escapeForRegex(value) {
  return value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")
}

function findMatchingBrace(source, openIndex) {
  let depth = 0
  for (let i = openIndex; i < source.length; i++) {
    const c = source[i]
    if (c === "{") {
      depth += 1
    } else if (c === "}") {
      depth -= 1
      if (depth === 0) {
        return i
      }
    }
  }

  return -1
}

function extractMethodBodies(source, signatureRegex) {
  const bodies = []

  for (const match of source.matchAll(signatureRegex)) {
    if (match.index === undefined) {
      continue
    }

    const openBraceIndex = source.indexOf("{", match.index)
    if (openBraceIndex < 0) {
      continue
    }

    const closeBraceIndex = findMatchingBrace(source, openBraceIndex)
    if (closeBraceIndex < 0) {
      continue
    }

    bodies.push(source.slice(openBraceIndex + 1, closeBraceIndex))
  }

  return bodies
}

function normalizeShortOptBranches(body) {
  const ternaryToFullOption =
    /(?:shortOpt|optType\s*==\s*OptType::SHORT)\s*\?\s*([\s\S]*?)\s*(?<!:):(?!:)\s*([\s\S]*?)(?=[,)\n])/g

  return body.replace(ternaryToFullOption, "$2")
}

function extractFilterModeSelectionReturnBody(menuItemCorpus, slot) {
  const bodies = [
    ...extractMethodBodies(
      menuItemCorpus,
      /FilterModeSelection::getOptions\s*\([^)]*\)/g,
    ),
  ]

  const classBody = extractClassBody(menuItemCorpus, "FilterModeSelection")
  if (classBody) {
    bodies.push(
      ...extractMethodBodies(
        classBody,
        /getOptions\s*\([^)]*\)\s*(?:override\s*)?/g,
      ),
    )
  }

  for (const body of bodies) {
    const slotCheckMatch =
      /if\s*\(\s*info\.getSlot\(\)\s*==\s*FilterSlot::HPF\s*\)\s*\{/.exec(body)
    if (!slotCheckMatch || slotCheckMatch.index === undefined) {
      continue
    }

    const ifOpenIndex = body.indexOf("{", slotCheckMatch.index)
    if (ifOpenIndex < 0) {
      continue
    }

    const ifCloseIndex = findMatchingBrace(body, ifOpenIndex)
    if (ifCloseIndex < 0) {
      continue
    }

    const hpfBranchBody = body.slice(ifOpenIndex + 1, ifCloseIndex)
    const lpfBranchBody = body.slice(ifCloseIndex + 1)
    const selectedBranchBody = slot === "HPF" ? hpfBranchBody : lpfBranchBody
    const returnMatch = /return\s*\{([\s\S]*?)\};/.exec(selectedBranchBody)

    if (returnMatch) {
      return normalizeShortOptBranches(returnMatch[1])
    }
  }

  return null
}

function extractSelectionTokensFromBody(body) {
  const tokens = []
  for (const tokenMatch of normalizeShortOptBranches(body).matchAll(
    /\bSTRING_FOR_[A-Z0-9_]+\b/g,
  )) {
    tokens.push(tokenMatch[0])
  }
  return tokens
}

function extractSelectionLiteralsFromBody(body) {
  const literals = []
  for (const literalMatch of normalizeShortOptBranches(body).matchAll(
    /"([^"\\n]+)"/g,
  )) {
    literals.push(literalMatch[1])
  }
  return literals
}

function extractReturnedSelectionTokensForFunction(
  menuItemCorpus,
  functionName,
) {
  const tokens = []
  const bodies = extractMethodBodies(
    menuItemCorpus,
    new RegExp(`${escapeForRegex(functionName)}\\s*\\([^)]*\\)`, "g"),
  )

  for (const body of bodies) {
    tokens.push(...extractSelectionTokensFromBody(body))
  }

  return unique(tokens)
}

function extractClassBody(source, typeName) {
  const typeParts = typeName.split("::")
  const className = typeParts.at(-1) ?? typeName
  const namespaceHint = typeParts.length > 1 ? typeParts.at(-2) : null

  const candidates = []
  const classPattern = new RegExp(
    `class\\s+${escapeForRegex(className)}\\b`,
    "g",
  )

  for (const match of source.matchAll(classPattern)) {
    if (match.index === undefined) {
      continue
    }

    const openBraceIndex = source.indexOf("{", match.index)
    if (openBraceIndex < 0) {
      continue
    }

    const closeBraceIndex = findMatchingBrace(source, openBraceIndex)
    if (closeBraceIndex < 0) {
      continue
    }

    const prefix = source.slice(Math.max(0, match.index - 1200), match.index)
    const namespaceMatch = namespaceHint
      ? new RegExp(
          `namespace\\s+[^\\n{;]*\\b${escapeForRegex(namespaceHint)}\\b`,
        ).test(prefix)
      : false

    candidates.push({
      body: source.slice(openBraceIndex + 1, closeBraceIndex),
      namespaceMatch,
    })
  }

  const best = candidates.find((candidate) => candidate.namespaceMatch)
  return (best ?? candidates[0])?.body ?? null
}

function simpleTypeName(typeName) {
  return typeName.split("::").at(-1) ?? typeName
}

function extractDirectBaseTypes(source, typeName) {
  const typeParts = typeName.split("::")
  const className = typeParts.at(-1) ?? typeName
  const namespaceHint = typeParts.length > 1 ? typeParts.at(-2) : null

  const candidates = []
  const classPattern = new RegExp(
    `class\\s+${escapeForRegex(className)}\\b`,
    "g",
  )

  for (const match of source.matchAll(classPattern)) {
    if (match.index === undefined) {
      continue
    }

    const openBraceIndex = source.indexOf("{", match.index)
    if (openBraceIndex < 0) {
      continue
    }

    const declaration = source.slice(match.index, openBraceIndex)
    const namespacePrefix = source.slice(
      Math.max(0, match.index - 1200),
      match.index,
    )
    const namespaceMatch = namespaceHint
      ? new RegExp(
          `namespace\\s+[^\\n{;]*\\b${escapeForRegex(namespaceHint)}\\b`,
        ).test(namespacePrefix)
      : false

    const baseTypes = []
    for (const baseMatch of declaration.matchAll(
      /\bpublic\s+([A-Za-z_][A-Za-z0-9_:]*)/g,
    )) {
      baseTypes.push(baseMatch[1])
    }

    candidates.push({
      baseTypes,
      namespaceMatch,
    })
  }

  const best = candidates.find((candidate) => candidate.namespaceMatch)
  return (best ?? candidates[0])?.baseTypes ?? []
}

function typeExtends(
  typeName,
  targetTypeName,
  sourceStructure,
  seen = new Set(),
) {
  if (!typeName) {
    return false
  }

  const cacheKey = `${typeName}->${targetTypeName}`
  const cached = sourceStructure.typeInheritanceCache.get(cacheKey)
  if (cached !== undefined) {
    return cached
  }

  const currentSimple = simpleTypeName(typeName)
  if (currentSimple === targetTypeName) {
    sourceStructure.typeInheritanceCache.set(cacheKey, true)
    return true
  }

  if (seen.has(typeName)) {
    sourceStructure.typeInheritanceCache.set(cacheKey, false)
    return false
  }
  seen.add(typeName)

  const baseTypes = extractDirectBaseTypes(
    sourceStructure.menuItemCorpus,
    typeName,
  )
  for (const baseType of baseTypes) {
    if (simpleTypeName(baseType) === targetTypeName) {
      sourceStructure.typeInheritanceCache.set(cacheKey, true)
      return true
    }
    if (typeExtends(baseType, targetTypeName, sourceStructure, seen)) {
      sourceStructure.typeInheritanceCache.set(cacheKey, true)
      return true
    }
  }

  sourceStructure.typeInheritanceCache.set(cacheKey, false)
  return false
}

function extractSelectionOptionsTokensForType(menuItemCorpus, typeName) {
  const tokens = []

  const qualifiedBodies = extractMethodBodies(
    menuItemCorpus,
    new RegExp(`${escapeForRegex(typeName)}::getOptions\\s*\\([^)]*\\)`, "g"),
  )
  for (const body of qualifiedBodies) {
    tokens.push(...extractSelectionTokensFromBody(body))
  }

  const classBody = extractClassBody(menuItemCorpus, typeName)
  if (classBody) {
    const unqualifiedBodies = extractMethodBodies(
      classBody,
      /getOptions\s*\([^)]*\)\s*(?:override\s*)?/g,
    )
    for (const body of unqualifiedBodies) {
      tokens.push(...extractSelectionTokensFromBody(body))
    }
  }

  return unique(tokens)
}

function extractSelectionOptionLiteralsForType(menuItemCorpus, typeName) {
  const literals = []

  const qualifiedBodies = extractMethodBodies(
    menuItemCorpus,
    new RegExp(`${escapeForRegex(typeName)}::getOptions\\s*\\([^)]*\\)`, "g"),
  )
  for (const body of qualifiedBodies) {
    literals.push(...extractSelectionLiteralsFromBody(body))
  }

  const classBody = extractClassBody(menuItemCorpus, typeName)
  if (classBody) {
    const unqualifiedBodies = extractMethodBodies(
      classBody,
      /getOptions\s*\([^)]*\)\s*(?:override\s*)?/g,
    )
    for (const body of unqualifiedBodies) {
      literals.push(...extractSelectionLiteralsFromBody(body))
    }
  }

  return unique(literals).filter(Boolean)
}

function extractDisplayConditionalSingleOptionPairForType(
  menuItemCorpus,
  typeName,
) {
  const bodies = []

  const typeCandidates = unique([typeName, simpleTypeName(typeName)]).filter(
    Boolean,
  )

  for (const candidate of typeCandidates) {
    const qualifiedBodies = extractMethodBodies(
      menuItemCorpus,
      new RegExp(
        `${escapeForRegex(candidate)}::getOptions\\s*\\([^)]*\\)`,
        "g",
      ),
    )
    bodies.push(...qualifiedBodies)
  }

  const classBody = extractClassBody(menuItemCorpus, typeName)
  if (classBody) {
    const unqualifiedBodies = extractMethodBodies(
      classBody,
      /getOptions\s*\([^)]*\)\s*(?:override\s*)?/g,
    )
    bodies.push(...unqualifiedBodies)
  }

  for (const body of bodies) {
    if (!/display->haveOLED\s*\(\s*\)/.test(body)) {
      continue
    }

    // Heuristic: display-conditional selectors with one option on each branch
    // should have exactly two option tokens total and at least two returns of
    // the `{..., 1}` form.
    const tokens = extractSelectionTokensFromBody(body)
    if (tokens.length !== 2) {
      continue
    }

    const singleOptionReturns =
      body.match(/return\s*\{[^}]*,\s*1\s*\}\s*;/g)?.length ?? 0
    if (singleOptionReturns < 2) {
      continue
    }

    return {
      oledToken: tokens[0],
      sevenSegToken: tokens[1],
    }
  }

  return null
}

function buildImplicitOptionChildren(
  varName,
  english,
  sevenSeg,
  sourceStructure,
) {
  const selectorChildren = buildSelectorOptionChildren(
    varName,
    english,
    sevenSeg,
    sourceStructure,
    0,
  )
  if (selectorChildren.length > 0) {
    return selectorChildren
  }

  const typeName = sourceStructure.varToType.get(varName) ?? ""
  if (!typeExtends(typeName, "Toggle", sourceStructure)) {
    return []
  }

  return [
    makeVirtualNode(
      english,
      sevenSeg,
      "STRING_FOR_DISABLED",
      `virtualToggle_${varName}_0`,
    ),
    makeVirtualNode(
      english,
      sevenSeg,
      "STRING_FOR_ENABLED",
      `virtualToggle_${varName}_1`,
    ),
  ].filter(Boolean)
}

function extractSelectButtonTargetForType(menuItemCorpus, typeName) {
  const typeCandidates = unique([typeName, simpleTypeName(typeName)]).filter(
    Boolean,
  )

  for (const candidate of typeCandidates) {
    const pattern = new RegExp(
      `${escapeForRegex(candidate)}::selectButtonPress\\s*\\([^)]*\\)\\s*\\{[\\s\\S]*?return\\s*&\\s*([A-Za-z_][A-Za-z0-9_:]*)\\s*;`,
      "g",
    )
    const match = pattern.exec(menuItemCorpus)
    if (match) {
      return stripNamespace(match[1])
    }

    const qualifiedBodies = extractMethodBodies(
      menuItemCorpus,
      new RegExp(
        `${escapeForRegex(candidate)}::selectButtonPress\\s*\\([^)]*\\)`,
        "g",
      ),
    )
    for (const body of qualifiedBodies) {
      const openUiMatch =
        /openUI\s*\(\s*&\s*([A-Za-z_][A-Za-z0-9_:]*)\s*\)/.exec(body)
      if (openUiMatch) {
        return stripNamespace(openUiMatch[1])
      }
    }
  }

  const classBody = extractClassBody(menuItemCorpus, typeName)
  if (classBody) {
    const unqualifiedBodies = extractMethodBodies(
      classBody,
      /selectButtonPress\s*\([^)]*\)\s*(?:override\s*)?/g,
    )
    for (const body of unqualifiedBodies) {
      const match = /return\s*&\s*([A-Za-z_][A-Za-z0-9_:]*)\s*;/.exec(body)
      if (match) {
        return stripNamespace(match[1])
      }

      const openUiMatch =
        /openUI\s*\(\s*&\s*([A-Za-z_][A-Za-z0-9_:]*)\s*\)/.exec(body)
      if (openUiMatch) {
        return stripNamespace(openUiMatch[1])
      }
    }
  }

  return null
}

function resolveVarTypeFromCorpus(menuItemCorpus, varName) {
  const pattern = new RegExp(
    `(?:^|\\n)\\s*(?:extern\\s+)?([A-Za-z_][A-Za-z0-9_:\\s<>*&]*?)\\s+${escapeForRegex(varName)}\\s*\\{`,
    "m",
  )
  const match = pattern.exec(menuItemCorpus)
  if (!match) {
    const externPattern = new RegExp(
      `(?:^|\\n)\\s*extern\\s+([A-Za-z_][A-Za-z0-9_:\\s<>*&]*?)\\s+${escapeForRegex(varName)}\\s*;`,
      "m",
    )
    const externMatch = externPattern.exec(menuItemCorpus)
    if (!externMatch) {
      return null
    }
    return normalizeTypeName(externMatch[1])
  }
  return normalizeTypeName(match[1])
}

function extractSizeReturnForType(menuItemCorpus, typeName) {
  const typeCandidates = unique([typeName, simpleTypeName(typeName)]).filter(
    Boolean,
  )

  for (const candidate of typeCandidates) {
    const qualifiedMatch = new RegExp(
      `${escapeForRegex(candidate)}::size\\s*\\([^)]*\\)\\s*(?:const\\s*)?(?:override\\s*)?\\{[\\s\\S]*?return\\s+([A-Za-z_][A-Za-z0-9_]*|\\d+)\\s*;`,
      "m",
    ).exec(menuItemCorpus)
    if (qualifiedMatch) {
      return qualifiedMatch[1]
    }
  }

  const classBody = extractClassBody(menuItemCorpus, typeName)
  if (!classBody) {
    return null
  }

  const unqualifiedMatch =
    /size\s*\([^)]*\)\s*(?:const\s*)?(?:override\s*)?\{[\s\S]*?return\s+([A-Za-z_][A-Za-z0-9_]*|\d+)\s*;/.exec(
      classBody,
    )
  return unqualifiedMatch ? unqualifiedMatch[1] : null
}

function resolveTypeSize(typeName, sourceStructure, seen = new Set()) {
  if (!typeName || seen.has(typeName)) {
    return null
  }
  seen.add(typeName)

  const sizeExpr = extractSizeReturnForType(
    sourceStructure.menuItemCorpus,
    typeName,
  )
  if (sizeExpr) {
    if (/^\d+$/.test(sizeExpr)) {
      return Number.parseInt(sizeExpr, 10)
    }
    const definedValue = sourceStructure.numericDefines.get(sizeExpr)
    if (definedValue !== undefined) {
      return definedValue
    }
  }

  const baseTypes = extractDirectBaseTypes(
    sourceStructure.menuItemCorpus,
    typeName,
  )
  for (const baseType of baseTypes) {
    const resolved = resolveTypeSize(baseType, sourceStructure, seen)
    if (resolved !== null) {
      return resolved
    }
  }

  return null
}

function buildSyncLevelChildren(varName, sourceStructure) {
  if (!sourceStructure.syncOptionContext) {
    return []
  }

  const typeName = sourceStructure.varToType.get(varName) ?? ""
  const size = resolveTypeSize(typeName, sourceStructure)
  const maxCount = size ?? 0

  const children = []
  for (let option = 0; option < Math.max(0, maxCount); option += 1) {
    const label =
      option === 0
        ? sourceStructure.syncOffLabel
          ? {
              oled: sourceStructure.syncOffLabel.oled,
              code: sourceStructure.syncOffLabel.code,
              token: sourceStructure.syncOffLabel.token,
            }
          : buildSyncLabelForOption(option, sourceStructure.syncOptionContext)
        : buildSyncLabelForOption(option, sourceStructure.syncOptionContext)

    if (!label) {
      continue
    }

    children.push({
      varName: `virtualSync_${varName}_${option}`,
      token: label.token ?? `LITERAL_${varName}_${option}`,
      oled: label.oled,
      code: label.code,
      children: [],
    })
  }

  return children
}

function buildIteranceChildren(varName, customVarName, sourceStructure) {
  const customPreset = sourceStructure.iteranceCustomPreset
  if (customPreset === undefined) {
    return []
  }

  const customRoot = buildNode(
    sourceStructure.combined,
    sourceStructure.arrayChildren,
    sourceStructure.varToToken,
    sourceStructure.english,
    sourceStructure.sevenSeg,
    sourceStructure,
    customVarName,
    [varName],
    1,
  )

  return sourceStructure.iteranceOptionLabels.map((label, index) => {
    const presetIndex = index
    const isCustom = presetIndex === customPreset
    return {
      varName: `virtualIterance_${varName}_${presetIndex}`,
      token: `LITERAL_${varName}_${presetIndex}`,
      oled: label.oled,
      code: label.code,
      children: isCustom ? (customRoot?.children ?? []) : [],
    }
  })
}

function buildSelectorOptionChildren(
  selectorVar,
  english,
  sevenSeg,
  sourceStructure,
  depth,
) {
  if (depth > 4) {
    return []
  }

  const selectorType = sourceStructure.varToType.get(selectorVar)
  const resolvedSelectorType =
    selectorType ??
    resolveVarTypeFromCorpus(sourceStructure.menuItemCorpus, selectorVar)
  if (!resolvedSelectorType) {
    return []
  }

  const selectorSimpleType = simpleTypeName(resolvedSelectorType)
  if (selectorSimpleType === "FilterModeSelection") {
    const selectorInitializerBody = extractInitializerBody(
      sourceStructure.combined,
      selectorVar,
    )
    const filterSlot = /FilterSlot::HPF/.test(selectorInitializerBody ?? "")
      ? "HPF"
      : /FilterSlot::LPF/.test(selectorInitializerBody ?? "")
        ? "LPF"
        : null

    if (filterSlot) {
      const filterModeBody = extractFilterModeSelectionReturnBody(
        sourceStructure.menuItemCorpus,
        filterSlot,
      )
      if (filterModeBody) {
        const optionTokens = extractSelectionTokensFromBody(filterModeBody)
        if (optionTokens.length === 0) {
          return []
        }

        const nextSelector = extractSelectButtonTargetForType(
          sourceStructure.menuItemCorpus,
          resolvedSelectorType,
        )
        const nextChildren = nextSelector
          ? buildSelectorOptionChildren(
              nextSelector,
              english,
              sevenSeg,
              sourceStructure,
              depth + 1,
            )
          : []

        return optionTokens
          .map((token, index) =>
            makeVirtualNode(
              english,
              sevenSeg,
              token,
              `virtualSelector_${selectorVar}_${index}`,
              cloneChildren(nextChildren),
            ),
          )
          .filter(Boolean)
      }
    }
  }

  if (selectorSimpleType === "Type" && selectorVar === "modFXTypeMenu") {
    const modFxNames = extractReturnedSelectionTokensForFunction(
      `${sourceStructure.menuItemCorpus}\n\n${sourceStructure.globalEffectableCpp}`,
      "getModNames",
    )
    if (modFxNames.length > 0) {
      const nextChildren = []
      return modFxNames
        .map((token, index) =>
          makeVirtualNode(
            english,
            sevenSeg,
            token,
            `virtualSelector_${selectorVar}_${index}`,
            cloneChildren(nextChildren),
          ),
        )
        .filter(Boolean)
    }
  }

  const optionTokens = extractSelectionOptionsTokensForType(
    sourceStructure.menuItemCorpus,
    resolvedSelectorType,
  )
  const optionLiterals = extractSelectionOptionLiteralsForType(
    sourceStructure.menuItemCorpus,
    resolvedSelectorType,
  )
  const nextSelector = extractSelectButtonTargetForType(
    sourceStructure.menuItemCorpus,
    resolvedSelectorType,
  )
  const nextChildren = nextSelector
    ? buildSelectorOptionChildren(
        nextSelector,
        english,
        sevenSeg,
        sourceStructure,
        depth + 1,
      )
    : []

  if (optionTokens.length === 0 && optionLiterals.length === 0) {
    return nextChildren
  }

  const displayConditionalPair =
    extractDisplayConditionalSingleOptionPairForType(
      sourceStructure.menuItemCorpus,
      resolvedSelectorType,
    )
  if (displayConditionalPair && optionLiterals.length === 0) {
    const confirmNode = makeVirtualNode(
      english,
      sevenSeg,
      displayConditionalPair.oledToken,
      `virtualSelector_${selectorVar}_confirm`,
      cloneChildren(nextChildren),
      displayConditionalPair.sevenSegToken,
    )

    return confirmNode ? [confirmNode] : []
  }

  const tokenChildren = optionTokens
    .map((token, index) =>
      makeVirtualNode(
        english,
        sevenSeg,
        token,
        `virtualSelector_${selectorVar}_${index}`,
        cloneChildren(nextChildren),
      ),
    )
    .filter(Boolean)

  const literalChildren = optionLiterals.map((value, index) => ({
    varName: `virtualSelectorLiteral_${selectorVar}_${index}`,
    token: `LITERAL_${selectorVar}_${index}`,
    oled: value,
    code: value,
    children: cloneChildren(nextChildren),
  }))

  return [...tokenChildren, ...literalChildren]
}

function buildDevicesFromSource(english, sevenSeg, sourceStructure) {
  const { combined, arrayChildren, varToToken } = sourceStructure

  const templateVarNames = extractChildren(
    combined,
    arrayChildren,
    "midiDeviceMenu",
  )
  const templateChildren = templateVarNames
    .map((childVar) => {
      const selectorOptions = buildSelectorOptionChildren(
        childVar,
        english,
        sevenSeg,
        sourceStructure,
        0,
      )
      if (selectorOptions.length > 0) {
        const label = labelFromToken(
          english,
          sevenSeg,
          varToToken.get(childVar),
        )
        if (!label) {
          return null
        }
        return {
          varName: childVar,
          token: label.token,
          oled: label.oled,
          code: label.code,
          children: selectorOptions,
        }
      }

      return buildNode(
        combined,
        arrayChildren,
        varToToken,
        english,
        sevenSeg,
        sourceStructure,
        childVar,
        ["devicesMenu", "midiDeviceMenu"],
        1,
      )
    })
    .filter(Boolean)

  const devicesLabel = labelFromToken(english, sevenSeg, "STRING_FOR_DEVICES")
  if (!devicesLabel) {
    return []
  }

  return [
    {
      varName: "virtualDeviceRepresentative",
      token: devicesLabel.token,
      oled: "Device",
      code: devicesLabel.code,
      children: cloneChildren(templateChildren),
    },
  ]
}

function buildDynamicSelectionChildren(
  varName,
  english,
  sevenSeg,
  sourceStructure,
) {
  if (varName === "audioSourceSelectorMenu") {
    return buildSelectorOptionChildren(
      "audioInputSelector",
      english,
      sevenSeg,
      sourceStructure,
      0,
    )
  }

  if (
    varName === "swingIntervalMenu" ||
    varName === "defaultSwingIntervalMenu"
  ) {
    return buildSyncLevelChildren(varName, sourceStructure)
  }

  if (varName === "noteIteranceMenu") {
    return buildIteranceChildren(
      varName,
      "noteCustomIteranceRootMenu",
      sourceStructure,
    )
  }

  if (varName === "noteRowIteranceMenu") {
    return buildIteranceChildren(
      varName,
      "noteRowCustomIteranceRootMenu",
      sourceStructure,
    )
  }

  const typeName = sourceStructure.varToType.get(varName) ?? ""
  if (typeExtends(typeName, "SyncLevel", sourceStructure)) {
    return buildSyncLevelChildren(varName, sourceStructure)
  }

  if (varName === "noteFillMenu" || varName === "noteRowFillMenu") {
    return sourceStructure.fillOptionLabels.map((label, index) => ({
      varName: `virtualFill_${varName}_${index}`,
      token: `LITERAL_${varName}_${index}`,
      oled: label,
      code: label,
      children: [],
    }))
  }

  if (varName === "activeScaleMenu" || varName === "defaultActiveScaleMenu") {
    return sourceStructure.presetScaleNames.map((scaleName, index) => ({
      varName: `virtualActiveScale_${varName}_${index}`,
      token: `LITERAL_${varName}_${index}`,
      oled: scaleName,
      code: scaleName,
      children: [],
    }))
  }

  if (varName === "cvSelectionMenu") {
    const volts = makeVirtualNode(
      english,
      sevenSeg,
      "STRING_FOR_VOLTS_PER_OCTAVE",
      "virtualCvVolts",
      [],
      "STRING_FOR_CV_V_PER_OCTAVE_MENU_TITLE",
    )
    const transpose = makeVirtualNode(
      english,
      sevenSeg,
      "STRING_FOR_TRANSPOSE",
      "virtualCvTranspose",
      [],
      "STRING_FOR_CV_TRANSPOSE_MENU_TITLE",
    )

    const cv1 = makeVirtualNode(
      english,
      sevenSeg,
      "STRING_FOR_CV_OUTPUT_1",
      "virtualCvOutput1",
      [volts, transpose].filter(Boolean),
    )
    const cv2 = makeVirtualNode(
      english,
      sevenSeg,
      "STRING_FOR_CV_OUTPUT_2",
      "virtualCvOutput2",
      [volts, transpose].filter(Boolean),
    )

    return [cv1, cv2].filter(Boolean)
  }

  if (varName === "gateSelectionMenu") {
    const vTrig = makeVirtualNode(
      english,
      sevenSeg,
      "STRING_FOR_V_TRIGGER",
      "virtualGateVTrig",
    )
    const sTrig = makeVirtualNode(
      english,
      sevenSeg,
      "STRING_FOR_S_TRIGGER",
      "virtualGateSTrig",
    )
    const run = makeVirtualNode(
      english,
      sevenSeg,
      "STRING_FOR_RUN_SIGNAL",
      "virtualGateRun",
    )
    const clock = makeVirtualNode(
      english,
      sevenSeg,
      "STRING_FOR_CLOCK",
      "virtualGateClock",
    )

    const out1 = makeVirtualNode(
      english,
      sevenSeg,
      "STRING_FOR_GATE_OUTPUT_1",
      "virtualGateOutput1",
      [vTrig, sTrig].filter(Boolean),
    )
    const out2 = makeVirtualNode(
      english,
      sevenSeg,
      "STRING_FOR_GATE_OUTPUT_2",
      "virtualGateOutput2",
      [vTrig, sTrig].filter(Boolean),
    )
    const out3 = makeVirtualNode(
      english,
      sevenSeg,
      "STRING_FOR_GATE_OUTPUT_3",
      "virtualGateOutput3",
      [vTrig, sTrig, run].filter(Boolean),
    )
    const out4 = makeVirtualNode(
      english,
      sevenSeg,
      "STRING_FOR_GATE_OUTPUT_4",
      "virtualGateOutput4",
      [vTrig, sTrig, clock].filter(Boolean),
    )
    const offTime = makeVirtualNode(
      english,
      sevenSeg,
      "STRING_FOR_MINIMUM_OFF_TIME",
      "virtualGateOffTime",
    )

    return [out1, out2, out3, out4, offTime].filter(Boolean)
  }

  if (varName === "devicesMenu") {
    return buildDevicesFromSource(english, sevenSeg, sourceStructure)
  }

  if (varName === "patchCablesMenu") {
    return [
      {
        varName: "virtualPatchCableSelection",
        token: "LITERAL_patchCablesMenu_selected",
        oled: "Selected Patch Cable",
        code: "PATCH",
        children: [
          {
            varName: "virtualPatchCableRegular",
            token: "LITERAL_patchCablesMenu_regular",
            oled: "Regular destination",
            code: "REG",
            children: [
              {
                varName: "virtualPatchCableRegularStrength",
                token: "LITERAL_patchCablesMenu_regular_strength",
                oled: "Strength",
                code: "STR",
                children: [],
              },
            ],
          },
          {
            varName: "virtualPatchCableRange",
            token: "LITERAL_patchCablesMenu_range",
            oled: "Range destination",
            code: "RNG",
            children: [
              {
                varName: "virtualPatchCableRangeStrength",
                token: "LITERAL_patchCablesMenu_range_strength",
                oled: "Strength",
                code: "STR",
                children: [],
              },
            ],
          },
        ],
      },
    ]
  }

  if (varName === "runtimeFeatureSettingsMenu") {
    return buildRuntimeFeatureChildren(english, sevenSeg, sourceStructure)
  }

  return null
}

function buildSongClipSettingsVirtualTree(
  clipSettingsCpp,
  clipLaunchStyleCpp,
  english,
  sevenSeg,
) {
  const convert = labelFromToken(
    english,
    sevenSeg,
    "STRING_FOR_CONVERT_TO_AUDIO",
  )
  const clipMode = labelFromToken(english, sevenSeg, "STRING_FOR_CLIP_MODE")
  const clipName = labelFromToken(english, sevenSeg, "STRING_FOR_CLIP_NAME")
  const inf = labelFromToken(english, sevenSeg, "STRING_FOR_DEFAULT_LAUNCH")
  const fill = labelFromToken(english, sevenSeg, "STRING_FOR_FILL_LAUNCH")
  const once = labelFromToken(english, sevenSeg, "STRING_FOR_ONCE_LAUNCH")

  const menu = {
    varName: "songClipSettingsMenu",
    token: "VIRTUAL_SONG_CLIP_SETTINGS",
    oled: "Song Clip Settings",
    code: "CLIP",
    children: [],
  }

  if (convert) {
    menu.children.push({
      varName: "virtualConvertToAudio",
      token: convert.token,
      oled: convert.oled,
      code: convert.code,
      children: [],
    })
  }

  if (clipMode) {
    const clipModeChildren = [inf, fill, once]
      .filter(Boolean)
      .map((item, index) => ({
        varName: `virtualLaunchMode${index}`,
        token: item.token,
        oled: item.oled,
        code: item.code,
        children: [],
      }))

    menu.children.push({
      varName: "virtualClipMode",
      token: clipMode.token,
      oled: clipMode.oled,
      code: clipMode.code,
      children: clipModeChildren,
    })
  }

  if (clipName) {
    menu.children.push({
      varName: "virtualClipName",
      token: clipName.token,
      oled: clipName.oled,
      code: clipName.code,
      children: [],
    })
  }

  // Keep these reads to ensure this virtual tree tracks those source files
  // in generation and changes are visible in diffs when logic evolves.
  void clipSettingsCpp
  void clipLaunchStyleCpp

  return menu
}

function buildNode(
  sourceText,
  arrayChildren,
  varToToken,
  english,
  sevenSeg,
  sourceStructure,
  varName,
  ancestors,
  depth,
) {
  if (depth > 15) {
    return null
  }

  if (ancestors.includes(varName)) {
    return null
  }

  const token = varToToken.get(varName)
  if (!token) {
    return null
  }

  const label = labelFromToken(english, sevenSeg, token)
  if (!label) {
    return null
  }
  const varAwareLabel = applyVarNameLabelOverrides(varName, label)

  const dynamicChildren = buildDynamicSelectionChildren(
    varName,
    english,
    sevenSeg,
    sourceStructure,
  )
  if (dynamicChildren) {
    return {
      varName,
      token,
      oled: varAwareLabel.oled,
      code: varAwareLabel.code,
      children: dynamicChildren,
    }
  }

  const childVars = extractChildren(sourceText, arrayChildren, varName)
  const filteredChildVars =
    varName === "arpPatternMenu"
      ? childVars.filter((childVar) => childVar !== "arpNoteModeMenuForDrums")
      : childVars
  const childrenFromStructure = filteredChildVars
    .map((childVar) =>
      buildNode(
        sourceText,
        arrayChildren,
        varToToken,
        english,
        sevenSeg,
        sourceStructure,
        childVar,
        [...ancestors, varName],
        depth + 1,
      ),
    )
    .filter(Boolean)

  const selectionChildren = buildImplicitOptionChildren(
    varName,
    english,
    sevenSeg,
    sourceStructure,
  )

  const children =
    childrenFromStructure.length > 0 ? childrenFromStructure : selectionChildren

  const nodeStructuralKey = (node) => {
    const childKeys = (node.children ?? []).map((child) =>
      nodeStructuralKey(child),
    )
    return `${node.oled}|${node.code}|[${childKeys.join(";")}]`
  }

  const dedupeStructurallyEquivalentSiblings = (items) => {
    const seen = new Set()
    return items.filter((child) => {
      // Wildcard labels (e.g. Envelope *, Osc*) intentionally represent
      // distinct menu instances and should not be collapsed.
      if (child.oled?.includes("*") || child.code?.includes("*")) {
        return true
      }

      const key = nodeStructuralKey(child)
      if (seen.has(key)) {
        return false
      }
      seen.add(key)
      return true
    })
  }

  const dedupedChildren = dedupeStructurallyEquivalentSiblings(children)
  const finalChildren =
    varName === "arpPatternMenu"
      ? dedupedChildren.filter(
          (child) => child.varName !== "arpNoteModeMenuForDrums",
        )
      : dedupedChildren

  return {
    varName,
    token,
    oled: varAwareLabel.oled,
    code: varAwareLabel.code,
    children: finalChildren,
  }
}

try {
  build()
} catch (error) {
  console.error("[generate-menu-hierarchies] Generation failed.")
  if (error instanceof Error) {
    console.error(error.message)
  } else {
    console.error(error)
  }
  process.exitCode = 1
}
