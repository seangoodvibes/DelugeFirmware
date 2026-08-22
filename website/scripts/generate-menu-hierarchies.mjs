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
  english: path.join(repoRoot, "src/deluge/gui/l10n/english.json"),
  sevenSeg: path.join(repoRoot, "src/deluge/gui/l10n/seven_segment.json"),
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

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, "utf8"))
}

function stripNamespace(symbol) {
  return symbol.split("::").at(-1)
}

function unique(values) {
  return [...new Set(values)]
}

function collectVarToToken(sourceText) {
  const map = new Map()
  const declarationPattern =
    /^\s*(?:PLACE_SDRAM_BSS\s+|PLACE_SDRAM_DATA\s+)?[A-Za-z_][A-Za-z0-9_:\s<>,*&]*?\s+([A-Za-z_][A-Za-z0-9_:]*)\s*\{\s*(STRING_FOR_[A-Z0-9_]+)/gm

  for (const match of sourceText.matchAll(declarationPattern)) {
    map.set(stripNamespace(match[1]), match[2])
  }

  return map
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

function build() {
  const menusCpp = fs.readFileSync(input.menusCpp, "utf8")
  const generatedMenus = fs.readFileSync(input.generatedMenus, "utf8")
  const clipSettingsCpp = fs.readFileSync(input.clipSettings, "utf8")
  const clipLaunchStyleCpp = fs.readFileSync(input.clipLaunchStyle, "utf8")
  const english = readJson(input.english).strings
  const sevenSeg = readJson(input.sevenSeg).strings

  const combined = `${generatedMenus}\n${menusCpp}`
  const varToToken = new Map([
    ...collectVarToToken(generatedMenus),
    ...collectVarToToken(menusCpp),
  ])
  const arrayChildren = extractArrayChildren(combined)

  const trees = {}
  for (const [treeKey, rootVar] of Object.entries(MENU_TREES)) {
    const node = buildNode(
      combined,
      arrayChildren,
      varToToken,
      english,
      sevenSeg,
      rootVar,
      [],
      0,
    )
    if (node) {
      trees[treeKey] = node
    }
  }

  trees.songClipSettingsMenu = buildSongClipSettingsVirtualTree(
    clipSettingsCpp,
    clipLaunchStyleCpp,
    english,
    sevenSeg,
  )

  const payload = {
    generatedAt: new Date().toISOString(),
    sourceFiles: {
      menusCpp: path.relative(repoRoot, input.menusCpp),
      generatedMenus: path.relative(repoRoot, input.generatedMenus),
      clipSettings: path.relative(repoRoot, input.clipSettings),
      clipLaunchStyle: path.relative(repoRoot, input.clipLaunchStyle),
      english: path.relative(repoRoot, input.english),
      sevenSeg: path.relative(repoRoot, input.sevenSeg),
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

function buildDynamicSelectionChildren(varName, english, sevenSeg) {
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
  const children = childVars
    .map((childVar) =>
      buildNode(
        sourceText,
        arrayChildren,
        varToToken,
        english,
        sevenSeg,
        childVar,
        [...ancestors, varName],
        depth + 1,
      ),
    )
    .filter(Boolean)

  return {
    varName,
    token,
    oled: varAwareLabel.oled,
    code: varAwareLabel.code,
    children,
  }
}

build()
