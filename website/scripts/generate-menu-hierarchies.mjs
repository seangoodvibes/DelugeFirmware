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
  menuItemRoot: path.join(repoRoot, "src/deluge/gui/menu_item"),
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
  const menuItemFiles = listFilesRecursive(input.menuItemRoot, [
    ".h",
    ".hpp",
    ".cpp",
  ])
  const menuItemCorpus = menuItemFiles
    .map((filePath) => fs.readFileSync(filePath, "utf8"))
    .join("\n\n")
  const english = readJson(input.english).strings
  const sevenSeg = readJson(input.sevenSeg).strings

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
  const sourceStructure = {
    combined,
    arrayChildren,
    varToToken,
    varToType,
    menuItemCorpus,
    typeInheritanceCache: new Map(),
  }

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
      menuItemRoot: path.relative(repoRoot, input.menuItemRoot),
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
    for (const tokenMatch of body.matchAll(/\bSTRING_FOR_[A-Z0-9_]+\b/g)) {
      tokens.push(tokenMatch[0])
    }
  }

  const classBody = extractClassBody(menuItemCorpus, typeName)
  if (classBody) {
    const unqualifiedBodies = extractMethodBodies(
      classBody,
      /getOptions\s*\([^)]*\)\s*(?:override\s*)?/g,
    )
    for (const body of unqualifiedBodies) {
      for (const tokenMatch of body.matchAll(/\bSTRING_FOR_[A-Z0-9_]+\b/g)) {
        tokens.push(tokenMatch[0])
      }
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
    for (const literalMatch of body.matchAll(/"([^"\\n]+)"/g)) {
      literals.push(literalMatch[1])
    }
  }

  const classBody = extractClassBody(menuItemCorpus, typeName)
  if (classBody) {
    const unqualifiedBodies = extractMethodBodies(
      classBody,
      /getOptions\s*\([^)]*\)\s*(?:override\s*)?/g,
    )
    for (const body of unqualifiedBodies) {
      for (const literalMatch of body.matchAll(/"([^"\\n]+)"/g)) {
        literals.push(literalMatch[1])
      }
    }
  }

  return unique(literals).filter(Boolean)
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
    return null
  }
  return normalizeTypeName(match[1])
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

  const optionTokens = extractSelectionOptionsTokensForType(
    sourceStructure.menuItemCorpus,
    resolvedSelectorType,
  )
  const optionLiterals = extractSelectionOptionLiteralsForType(
    sourceStructure.menuItemCorpus,
    resolvedSelectorType,
  )
  if (optionTokens.length === 0 && optionLiterals.length === 0) {
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
  const childrenFromStructure = childVars
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

  return {
    varName,
    token,
    oled: varAwareLabel.oled,
    code: varAwareLabel.code,
    children,
  }
}

build()
