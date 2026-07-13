<script lang="ts">
  import { activeControl, type ShortcutControlFilter } from "../stores/control_store.js"
  import { activeShortcutGroup } from "../stores/group_store.js"
  import { activeView } from "../stores/view_store.js"

  export let control: ShortcutControlFilter;

  $: isActive = $activeControl === control.id;

  function onClick() {
    activeShortcutGroup.set(null)
    activeView.set(null)

    activeControl.update((oldValue) => {
      if (oldValue === control.id) {
        return null;
      }
      return control.id;
    });
  }
</script>

<button class:active={isActive} class="dc-filter-chip dc-control-chip" on:click={onClick}>
  {control.title}
</button>

<style>
  .dc-control-chip {
    --dc-control-bg: rgb(178 205 222);
    --dc-control-border: rgb(106 148 187);
    --dc-control-fg: rgb(41 50 66);

    display: inline-flex;
    align-items: center;
    justify-content: center;
    margin: 0;
    white-space: nowrap;
    border-radius: 9999px;
    border: 1px solid var(--dc-control-border);
    box-sizing: border-box;
    height: 1.875rem;
    min-height: 1.875rem;
    padding: 0 0.75rem;
    font-size: 0.875rem;
    line-height: normal;
    font-weight: 500;
    color: var(--dc-control-border);
    background: transparent;
  }

  .dc-control-chip.active {
    background-color: var(--dc-control-bg);
    border-color: var(--dc-control-border);
    color: var(--dc-control-fg);
  }

  .dc-control-chip:hover {
    filter: brightness(1.05);
  }

  .dc-control-chip:focus-visible {
    outline: 2px solid var(--dc-control-border);
    outline-offset: 2px;
  }

  .dc-control-chip:active {
    transform: translateY(1px);
  }
</style>