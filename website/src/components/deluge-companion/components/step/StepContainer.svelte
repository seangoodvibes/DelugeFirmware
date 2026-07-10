<script lang="ts">
  import {
    isStep,
    isSubstepContainer,
    type StepOrSubstep,
  } from "../../types/shortcut";
  import StepView from "./Step.svelte";
  import SubstepView from "./Substep.svelte";

  export let step: StepOrSubstep;
  export let inline: boolean = false;
</script>

<div
  class="rounded-md border border-[var(--sl-color-gray-5)] bg-[var(--sl-color-bg)] align-bottom shadow-sm"
  class:px-1={inline}
  class:px-2={!inline}
  class:py-1={!inline}
  class:inline-block={inline}
>
  {#if isStep(step)}
    <StepView bind:step bind:inline />
  {:else if isSubstepContainer(step)}
    <SubstepView bind:step bind:inline />
  {/if}
</div>
