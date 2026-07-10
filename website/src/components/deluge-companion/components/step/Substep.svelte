<script lang="ts">
  import StepView from "./Step.svelte";
  import type { SubstepContainer } from "../../types/shortcut";

  export let step: SubstepContainer;
  export let inline: boolean;
</script>

<div class="flex items-end gap-2">
  {#each step.substeps as substep, idx}
    {#if idx > 0}
      <span class="self-end text-xs">+</span>
    {/if}
    <StepView bind:step={substep} {inline} />
  {/each}
</div>
