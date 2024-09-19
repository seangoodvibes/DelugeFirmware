export default {
	eleventyNavigation: {
		key: (data) => {
			let parts = data.page?.url?.slice(0, -1)?.split("/")
			if (parts) return parts.slice(1).join("-")
		},
		parent: (data) => {
			let parts = data.page?.url?.slice(0, -1)?.split("/")
			if (parts) return parts.slice(1, -1).join("-")
		},
		title: data => (data.navigation?.title || data.title || "").replace(
			/\w\S*/g,
			t => t[0].toUpperCase() + t.slice(1).toLowerCase()
		),
		order: data => data.navigation?.order || 0
	}
}
