<script lang="ts">
  import type { Shortcut } from "../types/shortcut.js";
  import StepContainerView from "./step/StepContainer.svelte";
  import { Firmwares, firmwaresById } from "../data/firmware.js";
  import { viewsById } from "../data/views.js";
  import DelugeView from "./DelugeUi.svelte";
  import ParagraphView from "./ParagraphView.svelte";

  export let shortcut: Shortcut;
  $: firmwares = shortcut.firmware.map((f) => firmwaresById[f]);
  $: views = shortcut.views.map((v) => viewsById[v]);

  const viewClassByColor: Record<string, string> = {
    neutral: "dc-view-neutral",
    gold: "dc-view-gold",
    red: "dc-view-red",
    green: "dc-view-green",
    blue: "dc-view-blue",
    purple: "dc-view-purple",
  };

  const firmwareBadgeClassById: Record<number, string> = {
    [Firmwares.OFFICIAL]: "dc-badge-note",
    [Firmwares.COMMUNITY]: "dc-badge-tip",
  };

  let showDetails: boolean = false;

  function onStepsClicked() {
    showDetails = !showDetails;
  }
</script>

<div class="shortcut-card rounded-lg p-4 text-[var(--sl-color-text)]">
  <div class="shortcut-header">
    <div class="mb-0 flex flex-wrap gap-1 leading-none">
      {#each firmwares as firmware}
        <span
          class={`dc-view-chip dc-firmware-chip ${firmwareBadgeClassById[firmware.id] ?? "dc-badge-note"}`}
        >
          {firmware.title} Firmware
        </span>
      {/each}
      {#each views as view}
        <span
          class={`dc-view-chip ${viewClassByColor[view.color] ?? "dc-view-neutral"}`}
        >
          {view.title}
        </span>
      {/each}
    </div>
    <h3 class="shortcut-title">
      {shortcut.name}
    </h3>
  </div>
  <button
    class="shortcut-steps m-0 rounded-md p-1 text-left"
    on:click={onStepsClicked}
  >
    <span class="shortcut-steps-inner">
      {#each shortcut.steps as step}
        <StepContainerView bind:step />
      {/each}
    </span>
  </button>
  {#if shortcut.description}
    <p class="shortcut-description mt-2 mb-0 text-sm leading-6">
      {shortcut.description}
    </p>
  {/if}
  {#if shortcut.paragraphs.length > 0}
    <aside class="shortcut-aside mt-4 rounded-md px-3 py-1" data-type="tip">
      {#each shortcut.paragraphs as paragraph}
        <ParagraphView bind:paragraph />
      {/each}
    </aside>
  {/if}
  {#if showDetails}
    <div class="mt-4 border border-[var(--sl-color-gray-5)]">
      <DelugeView bind:steps={shortcut.steps} />
    </div>
  {/if}
</div>

<style>
  .shortcut-card {
    --dc-card-bg: rgb(70 84 104 / 0.22);
    --dc-card-border: rgb(92 107 132 / 0.45);
    --dc-strip-bg: rgb(83 98 120 / 0.3);
    --dc-strip-border: rgb(102 118 143 / 0.45);
    --dc-step-bg: rgb(13 18 30 / 0.45);
    --dc-step-border: rgb(103 118 143 / 0.5);
    --dc-aside-bg: rgb(83 98 120 / 0.24);
    --dc-aside-border: rgb(178 205 222 / 0.72);
    --dc-aside-fg: var(--sl-color-gray-2);

    border: 1px solid var(--dc-card-border);
    background: var(--dc-card-bg);
  }

  .shortcut-steps {
    border: 1px solid var(--dc-strip-border);
    background: var(--dc-strip-bg);
    display: inline-block;
    box-sizing: border-box;
    inline-size: fit-content;
    max-inline-size: 100%;
    min-inline-size: 0;
    align-self: flex-start;
    overflow-x: auto;
  }

  .shortcut-steps-inner {
    display: inline-flex;
    flex-wrap: wrap;
    align-items: flex-start;
    gap: 0.25rem;
    inline-size: fit-content;
    max-inline-size: 100%;
    min-inline-size: 0;
  }

  .shortcut-steps-inner > :global(*) {
    flex: 0 0 auto;
  }

  :global(html[data-theme="light"]) .shortcut-card {
    --dc-card-bg: rgb(241 245 249 / 0.92);
    --dc-card-border: rgb(148 163 184 / 0.55);
    --dc-strip-bg: rgb(226 232 240 / 0.9);
    --dc-strip-border: rgb(148 163 184 / 0.7);
    --dc-step-bg: rgb(248 250 252 / 0.96);
    --dc-step-border: rgb(148 163 184 / 0.8);
    --dc-aside-bg: rgb(226 232 240 / 0.82);
    --dc-aside-border: rgb(106 148 187 / 0.85);
    --dc-aside-fg: rgb(51 65 85);
  }

  .shortcut-header {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
    width: 100%;
    min-width: 0;
  }

  .shortcut-card {
    display: flex;
    flex-direction: column;
    align-items: stretch;
    width: 100%;
    min-width: 0;
    max-width: 100%;
  }

  .shortcut-title {
    display: block;
    width: 100%;
    max-width: 100%;
    margin: 0;
    padding: 0;
    font-size: 1.15rem !important;
    font-weight: 700 !important;
    line-height: 1.2;
    white-space: normal;
    overflow-wrap: anywhere;
    word-break: break-word;
  }

  .shortcut-steps-inner > :global(*) {
    align-self: start;
    margin-top: 0;
  }

  .shortcut-description {
    color: var(--sl-color-gray-2);
  }

  :global(html[data-theme="light"]) .shortcut-description {
    color: var(--sl-color-gray-3);
  }

  .shortcut-aside {
    display: inline-block;
    box-sizing: border-box;
    inline-size: fit-content;
    max-inline-size: 100%;
    align-self: flex-start;
    border-inline-start: 0.25rem solid var(--dc-aside-border);
    background: var(--dc-aside-bg);
    color: var(--dc-aside-fg);
  }

  .shortcut-aside :global(p) {
    margin-block: 0.5rem;
  }

  .dc-view-chip {
    display: inline-flex;
    align-items: center;
    white-space: nowrap;
    border-radius: 9999px;
    padding: 0.2rem 0.5rem;
    font-size: 0.75rem;
    line-height: 1.1;
    font-weight: 500;
    background-color: var(--dc-chip-bg);
    color: var(--dc-chip-fg);
  }

  .dc-firmware-chip {
    display: inline-flex;
    align-items: center;
    border-radius: 0.45rem;
    border: 1px solid var(--dc-badge-border);
    padding: 0.18rem 0.54rem;
    font-size: 0.75rem;
    line-height: 1.15;
    font-weight: 600;
    letter-spacing: 0;
    font-family: var(--sl-font-system-mono, ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New", monospace);
    background: var(--dc-badge-bg);
    color: var(--dc-badge-fg);
  }

  .dc-badge-note {
    --dc-badge-bg: rgb(38 67 188 / 0.22);
    --dc-badge-border: rgb(75 112 255);
    --dc-badge-fg: rgb(222 232 255);
  }

  .dc-badge-tip {
    --dc-badge-bg: rgb(106 39 152 / 0.24);
    --dc-badge-border: rgb(173 97 232);
    --dc-badge-fg: rgb(241 218 255);
  }

  :global(html[data-theme="light"]) .dc-badge-note {
    --dc-badge-bg: rgb(229 236 255);
    --dc-badge-border: rgb(76 108 220);
    --dc-badge-fg: rgb(36 55 123);
  }

  :global(html[data-theme="light"]) .dc-badge-tip {
    --dc-badge-bg: rgb(245 231 255);
    --dc-badge-border: rgb(138 77 184);
    --dc-badge-fg: rgb(84 30 119);
  }

  .dc-view-blue {
    --dc-chip-bg: rgb(178 205 222);
    --dc-chip-border: rgb(106 148 187);
    --dc-chip-fg: rgb(41 50 66);
  }

  .dc-view-gold {
    --dc-chip-bg: rgb(202 180 122);
    --dc-chip-border: rgb(171 135 71);
    --dc-chip-fg: rgb(49 31 23);
  }

  .dc-view-green {
    --dc-chip-bg: rgb(132 199 141);
    --dc-chip-border: rgb(56 145 73);
    --dc-chip-fg: rgb(12 34 19);
  }

  .dc-view-neutral {
    --dc-chip-bg: rgb(167 177 185);
    --dc-chip-border: rgb(97 111 121);
    --dc-chip-fg: rgb(34 38 42);
  }

  .dc-view-purple {
    --dc-chip-bg: rgb(226 187 236);
    --dc-chip-border: rgb(192 115 210);
    --dc-chip-fg: rgb(58 18 64);
  }

  .dc-view-red {
    --dc-chip-bg: rgb(235 182 182);
    --dc-chip-border: rgb(210 115 115);
    --dc-chip-fg: rgb(57 22 22);
  }
</style>
