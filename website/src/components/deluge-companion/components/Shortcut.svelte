<script lang="ts">
  import type { Shortcut } from "../types/shortcut.js";
  import StepContainerView from "./step/StepContainer.svelte";
  import { viewsById } from "../data/views.js";
  import DelugeView from "./DelugeUi.svelte";
  import ParagraphView from "./ParagraphView.svelte";

  export let shortcut: Shortcut;
  $: views = shortcut.views.map((v) => viewsById[v]);

  const viewClassByColor: Record<string, string> = {
    neutral: "dc-view-neutral",
    gold: "dc-view-gold",
    red: "dc-view-red",
    green: "dc-view-green",
    blue: "dc-view-blue",
    purple: "dc-view-purple",
  };

  let showDetails: boolean = false;

  function onStepsClicked() {
    showDetails = !showDetails;
  }
</script>

<div class="rounded-lg border border-[var(--sl-color-gray-5)] bg-[var(--sl-color-bg)] p-4 shadow-lg text-[var(--sl-color-text)]">
  <div class="shortcut-header">
    <div class="mb-0 flex flex-wrap gap-1 leading-none">
      {#each views as view}
        <span
          class={`dc-view-chip ${viewClassByColor[view.color] ?? "dc-view-neutral"}`}
        >
          {view.title}
        </span>
      {/each}
    </div>
    <h3 class="shortcut-title text-lg font-bold leading-none">
      {shortcut.name}
    </h3>
  </div>
  <button
    class="m-0 inline-flex max-w-full flex-wrap items-end gap-x-1 gap-y-2 rounded-md border border-[var(--sl-color-gray-5)] bg-[var(--sl-color-gray-6)] px-2 py-1 text-left"
    on:click={onStepsClicked}
  >
    {#each shortcut.steps as step}
      <StepContainerView bind:step />
    {/each}
  </button>
  {#each shortcut.paragraphs as paragraph}
    <ParagraphView bind:paragraph />
  {/each}
  {#if showDetails}
    <div class="mt-4 border border-[var(--sl-color-gray-5)]">
      <DelugeView bind:steps={shortcut.steps} />
    </div>
  {/if}
</div>

<style>
  .shortcut-header {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }

  .shortcut-title {
    margin: 0;
    padding: 0;
    line-height: 1;
  }

  .dc-view-chip {
    display: inline-block;
    white-space: nowrap;
    border-radius: 9999px;
    padding: 0.125rem 0.5rem;
    font-size: 0.75rem;
    line-height: 1;
    font-weight: 500;
    background-color: var(--dc-chip-bg);
    color: var(--dc-chip-fg);
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
