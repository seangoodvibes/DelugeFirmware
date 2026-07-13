import { writable } from "svelte/store"
import { Firmwares, firmwaresById } from "../data/firmware.js"
import type { Firmware } from "../types/shortcut.js"

export const allFirmwares = writable<Firmware[]>(Object.values(firmwaresById))

export const activeFirmware = writable<Firmwares | null>(null)
