/** @type {import("markdown-it").PluginSimple} */
export default function (md) {
	// add `)` and ']' as a termination character
	md.inline.ruler.at("text", createTextRuler())
	createWrappingRule({
		char: "|",
		tag: "seven-segment",
	})(md)

	md.inline.ruler.after("link", "deluge", (state, silent) => {
		let char = state.src.charCodeAt(state.pos)

		let openSquacket = "[".charCodeAt(0)
		let openParen = "(".charCodeAt(0)

		if (char == openSquacket) {
			let slice = state.src
				.slice(state.pos)
			let buttonMatch = slice
				.match(/\[([A-Za-z/ -]+) ?(off|blinking)?\]/)

			let screenMatch = slice
				.match(/\[\[([A-Za-z0-9.-]+)\|([A-Za-z0-9. -]+)\]\]/)

			if (buttonMatch) {
				let [whole, name, buttonState] = buttonMatch
				let tagname = name.toLowerCase().replace(/ \/? ?/, "-") + "-button"
				/** @type {import("markdown-it").Token} */
				let token
				if (!silent) {
					token = state.push("dbutton_open", tagname, 1)
					token.attrSet(
						"on",
						buttonState == "off" ? false : buttonState ?? "on"
					)
					token.attrSet("webc:keep", true)
				}
				state.pos += whole.length
				if (token) {
					state.push("dbutton_close", tagname, -1)
				}
				return true
			} else if (screenMatch) {
				let [whole, seg, oled] = screenMatch
				let tagname = `menu-item`
				/** @type {import("markdown-it").Token} */
				let token
				if (!silent) {
					token = state.push("dscreen_open", "menu-item", 1)
					token.attrSet(
						"seg",
						seg
					)
					let text = state.push("text", "", 0)
					text.content = oled
					token.attrSet("webc:keep", true)
				}
				state.pos += whole.length
				if (token) {
					state.push("dscreen_close", "menu-item", -1)
				}
				return true
			}
		} else if (char == openParen) {
			let match = state.src.slice(state.pos).match(/\(([A-Za-z◄►▼▲ -]+)\)/)
			if (match) {
				let [whole, name] = match
				name = name.replace("◄►", "-h").replace("▼▲", "-v")

				let tagname = name.toLowerCase().replace(/ \/? ?/, "-") + "-encoder"
				if (tagname == "output-volume-encoder") {
					tagname = "output-volume-pot"
				} else if (tagname == "scrollv-encoder") {
					tagname = "scroll-v-encoder"
				} else if (tagname == "scrollh-encoder") {
					tagname = "scroll-h-encoder"
				}
				/** @type {import("markdown-it").Token} */
				let token
				if (!silent) {
					token = state.push("dknob_open", tagname, 1)
					token.attrSet("webc:keep", true)
					state.push("dknob_close", tagname, -1)
				}
				state.pos += whole.length
				return true
			}
		}
		return false
	})
}

let createTextRuler = () => {
	function isTerminatorChar(ch) {
		switch (ch) {
			case 0x0a /* \n */:
			case 0x21 /* ! */:
			case 0x23 /* # */:
			case 0x24 /* $ */:
			case 0x25 /* % */:
			case 0x26 /* & */:
			case 0x28 /* ( */:
			case 0x29 /* ) */:
			case 0x2a /* * */:
			case 0x2b /* + */:
			case 0x2d /* - */:
			// case 0x2f /* / */:
			case 0x3a /* : */:
			case 0x3c /* < */:
			case 0x3d /* = */:
			case 0x3e /* > */:
			case 0x40 /* @ */:
			case 0x5b /* [ */:
			case 0x5c /* \ */:
			case 0x5d /* ] */:
			case 0x5e /* ^ */:
			case 0x5f /* _ */:
			case 0x60 /* ` */:
			case 0x7b /* { */:
			case 0x7c /* | */:
			case 0x7d /* } */:
			case 0x7e /* ~ */:
				return true
			default:
				return false
		}
	}

	return function text(state, silent) {
		var pos = state.pos

		while (pos < state.posMax && !isTerminatorChar(state.src.charCodeAt(pos))) {
			pos++
		}

		if (pos === state.pos) {
			return false
		}

		if (!silent) {
			state.pending += state.src.slice(state.pos, pos)
		}

		state.pos = pos

		return true
	}
}

let createWrappingRule =
	({
		tag,
		name = tag,
		char,
		before = "emphasis",
		repeats = 1,
		classname = null,
	}) =>
	md => {
		let targetCharacterCode = char.charCodeAt(0)

		function tokenize(state, silent) {
			let startCharacter = state.src.charAt(state.pos)
			let marker = startCharacter.charCodeAt(0)

			if (silent) {
				return false
			}

			if (marker != targetCharacterCode) {
				return false
			}

			let scanned = state.scanDelims(state.pos, true)
			let scanLength = scanned.length

			if (scanLength < repeats) {
				return false
			}

			let token

			if (repeats > 1 && scanLength % repeats) {
				token = state.push("text", "", 0)
				token.content = startCharacter
				scanLength--
			}

			for (let index = 0; index < scanLength; index += repeats) {
				token = state.push("text", "", 0)
				token.content = startCharacter.repeat(repeats)

				state.delimiters.push({
					marker,
					jump: index,
					token: state.tokens.length - 1,
					level: state.level,
					end: -1,
					open: scanned.can_open,
					close: scanned.can_close,
				})
			}

			state.pos += scanned.length

			return true
		}

		function postProcess(state) {
			let startDelim
			let endDelim
			let {delimiters} = state
			let loneMarkers = []
			let token
			for (let i = 0; i < state.delimiters.length; i++) {
				startDelim = delimiters[i]

				if (startDelim.marker !== targetCharacterCode) {
					continue
				}

				if (startDelim.end === -1) {
					continue
				}

				endDelim = delimiters[startDelim.end]

				token = state.tokens[startDelim.token]
				token.type = `${name}_open`
				if (classname) {
					token.attrs = token.attrs || []
					classname && token.attrs.push(["class", classname])
				}
				token.tag = tag
				token.nesting = 1
				token.markup = char.repeat(repeats)
				token.content = ""

				token = state.tokens[endDelim.token]
				token.type = `${name}_close`
				token.tag = tag
				token.nesting = -1
				token.markup = char.repeat(repeats)
				token.content = ""

				if (
					state.tokens[endDelim.token - 1].type == "text" &&
					state.tokens[endDelim.token - 1].content == char
				) {
					loneMarkers.push(endDelim.token - 1)
				}
			}

			while (loneMarkers.length) {
				let i = loneMarkers.pop()
				let j = i + 1

				while (
					j < state.tokens.length &&
					state.tokens[j].type === `${name}_close`
				) {
					j++
				}

				j--

				if (i !== j) {
					token = state.tokens[j]
					state.tokens[j] = state.tokens[i]
					state.tokens[i] = token
				}
			}
		}

		md.inline.ruler.before(before, name, tokenize)
		md.inline.ruler2.before(before, name, postProcess)
	}
