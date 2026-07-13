import { derived, writable } from "svelte/store"
import jsonData from "../data/v4.1.0.json"
import fuzzysort from "fuzzysort"
import { searchQuery } from "./search_store"
import { activeView } from "./view_store"
import { activeFirmware } from "./firmware_store"
import { activeShortcutGroup } from "./group_store"
import { activeControl } from "./control_store"
import { Firmwares } from "../data/firmware"
import { Control } from "../data/targets"
import type {
  Shortcut,
  StepOrSubstep,
  SubstepContainer,
} from "../types/shortcut"

type RawStep =
  | { action: number; control: number }
  | { substeps: { action: number; control: number }[] }

type RawShortcut = Omit<Shortcut, "steps" | "firmware"> & {
  steps: RawStep[]
  firmware?: number[]
}

const convertedRawShortcuts: Shortcut[] = (jsonData as RawShortcut[]).map(
  (shortcut) => {
    const steps: StepOrSubstep[] = shortcut.steps.map((step) => {
      if ("substeps" in step) {
        const convertedSubstepContainer: SubstepContainer = {
          substeps: step.substeps,
        }
        return convertedSubstepContainer
      }
      return step
    })

    return {
      ...shortcut,
      steps,
      firmware:
        shortcut.firmware && shortcut.firmware.length > 0
          ? shortcut.firmware
          : [Firmwares.OFFICIAL, Firmwares.COMMUNITY],
    }
  },
)

const rawShortcuts = writable(convertedRawShortcuts)

const stepContainsControl = (step: StepOrSubstep, control: number): boolean => {
  if ("substeps" in step) {
    return step.substeps.some((substep) => stepContainsControl(substep, control))
  }

  return step.control === control
}

const shortcutContainsControl = (shortcut: Shortcut, control: number) => {
  return shortcut.steps.some((step) => stepContainsControl(step, control))
}

const allShortcuts = derived(rawShortcuts, ($rawShortcuts) => {
  return $rawShortcuts.map((shortcut) => ({
    ...shortcut,
    fuzzysortPrepared: fuzzysort.prepare(`${shortcut.name}}`),
  }))
})

const filteredBySearch = derived(
  [allShortcuts, searchQuery],
  ([$shortcuts, $searchQuery]) => {
    return fuzzysort
      .go($searchQuery, $shortcuts, {
        key: "fuzzysortPrepared",
        threshold: -1000,
        all: true,
      })
      .map((result) => result.obj)
  },
)

const filteredByGroups = derived(
  [filteredBySearch, activeShortcutGroup],
  ([$shortcuts, $activeShortcutGroup]) => {
    if ($activeShortcutGroup === null) {
      return $shortcuts
    }

    return $shortcuts.filter(
      (shortcut) => shortcut.group === $activeShortcutGroup,
    )
  },
)

const filteredByFirmware = derived(
  [filteredByGroups, activeFirmware],
  ([$shortcuts, $activeFirmware]) => {
    if ($activeFirmware === null) {
      return $shortcuts
    }

    return $shortcuts.filter((shortcut) =>
      shortcut.firmware.includes($activeFirmware),
    )
  },
)

const filteredByViews = derived(
  [filteredByFirmware, activeView],
  ([$shortcuts, $activeView]) => {
    if ($activeView === null) {
      return $shortcuts
    }
    return $shortcuts.filter((shortcut) => shortcut.views.includes($activeView))
  },
)

const filteredByControls = derived(
  [filteredByViews, activeControl],
  ([$shortcuts, $activeControl]) => {
    if ($activeControl === null) {
      return $shortcuts
    }

    return $shortcuts.filter((shortcut) =>
      shortcutContainsControl(shortcut, $activeControl),
    )
  },
)

export const availableGroupIds = derived(
  [filteredBySearch, activeFirmware, activeView, activeControl, activeShortcutGroup],
  ([
    $shortcuts,
    $activeFirmware,
    $activeView,
    $activeControl,
    $activeShortcutGroup,
  ]) => {
    const matching = $shortcuts.filter((shortcut) => {
      if (
        $activeFirmware !== null &&
        !shortcut.firmware.includes($activeFirmware)
      ) {
        return false
      }

      if ($activeView !== null && !shortcut.views.includes($activeView)) {
        return false
      }

      if (
        $activeControl !== null &&
        !shortcutContainsControl(shortcut, $activeControl)
      ) {
        return false
      }

      return true
    })

    const ids = new Set(matching.map((shortcut) => shortcut.group))

    if ($activeShortcutGroup !== null) {
      ids.add($activeShortcutGroup)
    }

    return ids
  },
)

export const availableViews = derived(
  [filteredBySearch, activeShortcutGroup, activeFirmware, activeControl, activeView],
  ([
    $shortcuts,
    $activeShortcutGroup,
    $activeFirmware,
    $activeControl,
    $activeView,
  ]) => {
    const matching = $shortcuts.filter((shortcut) => {
      if ($activeShortcutGroup !== null && shortcut.group !== $activeShortcutGroup) {
        return false
      }

      if (
        $activeFirmware !== null &&
        !shortcut.firmware.includes($activeFirmware)
      ) {
        return false
      }

      if (
        $activeControl !== null &&
        !shortcutContainsControl(shortcut, $activeControl)
      ) {
        return false
      }

      return true
    })

    const ids = new Set(matching.flatMap((shortcut) => shortcut.views))

    if ($activeView !== null) {
      ids.add($activeView)
    }

    return ids
  },
)

export const availableFirmwares = derived(
  [filteredBySearch, activeShortcutGroup, activeView, activeControl, activeFirmware],
  ([
    $shortcuts,
    $activeShortcutGroup,
    $activeView,
    $activeControl,
    $activeFirmware,
  ]) => {
    const matching = $shortcuts.filter((shortcut) => {
      if ($activeShortcutGroup !== null && shortcut.group !== $activeShortcutGroup) {
        return false
      }

      if ($activeView !== null && !shortcut.views.includes($activeView)) {
        return false
      }

      if (
        $activeControl !== null &&
        !shortcutContainsControl(shortcut, $activeControl)
      ) {
        return false
      }

      return true
    })

    const ids = new Set(matching.flatMap((shortcut) => shortcut.firmware))

    if ($activeFirmware !== null) {
      ids.add($activeFirmware)
    }

    return ids
  },
)

export const availableControls = derived(
  [filteredBySearch, activeShortcutGroup, activeView, activeFirmware, activeControl],
  ([
    $shortcuts,
    $activeShortcutGroup,
    $activeView,
    $activeFirmware,
    $activeControl,
  ]) => {
    const matching = $shortcuts.filter((shortcut) => {
      if ($activeShortcutGroup !== null && shortcut.group !== $activeShortcutGroup) {
        return false
      }

      if ($activeView !== null && !shortcut.views.includes($activeView)) {
        return false
      }

      if (
        $activeFirmware !== null &&
        !shortcut.firmware.includes($activeFirmware)
      ) {
        return false
      }

      return true
    })

    const ids = new Set<Control>()

    for (const shortcut of matching) {
      for (const step of shortcut.steps) {
        if ("substeps" in step) {
          for (const substep of step.substeps) {
            ids.add(substep.control)
          }
        } else {
          ids.add(step.control)
        }
      }
    }

    if ($activeControl !== null) {
      ids.add($activeControl)
    }

    return ids
  },
)

export const filteredShortcuts = derived(
  [filteredByControls],
  ([$shortcuts]) => {
    return $shortcuts
  },
)
