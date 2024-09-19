export function create(collection) {
	let navigation = [{}]

	for (let item of collection) {
		let path = item.url.split("/").slice(1, -1)
		let target = navigation[0]
		while (path.length) {
			let key = path.shift()
			if (!Array.isArray(target.children)) {
				target.children = []
			}

			let next = target.children.find(n => n.key == key)
			if (!next) {
				next = {}
				target.children.push(next)
			}
			target = next
			target.key = key
		}
		target.title =
			item.data.navigation.title ||
			item.data.title ||
			target.key.replace(
				/\w\S*/g,
				t => t[0].toUpperCase() + t.slice(1).toLowerCase()
			)
		target.order = item.data.navigation.order || 0
		target.url = item.url
		target.link = item.data.navigation.link !== false
		target.seg = item.data.navigation.seg
		target.name =
			target.title ||
			target.key.replace(
				/\w\S*/g,
				t => t[0].toUpperCase() + t.slice(1).toLowerCase()
			)
	}

	return navigation
}

export function html(navigation, pageURL) {
	return (
		navigation
			.sort((a, b) => a.order - b.order)
			.reduce((html, item) => {
				let title =
					item.url && item.link
						? `<a ${item.url == pageURL ? 'aria-current="page"' : ""} href="${
								item.url
						  }">${item.name}</a>`
						: `<span>${item.name}</span>`

				if (item.children?.length) {
					return (
						html +
						`<li><details open><summary>${title}</summary>` +
						this.html(item.children, pageURL) +
						`</details></li>`
					)
				} else {
					return html + `<li>${title}</li>`
				}
			}, `<ol>`) + `</ol>`
	)
}
