<script lang="ts">
  import {
    Control,
    controlDescriptions,
    ControlType,
  } from "../../data/targets";
  import CircleButton from "../../icons/CircleButton.svelte";
  import FullGrid from "../../icons/FullGrid.svelte";
  import Knob from "../../icons/Knob.svelte";
  import GridCol from "../../icons/GridCol.svelte";
  import Midi from "../../icons/Midi.svelte";
  import type { Step } from "../../types/shortcut";
  import { Action } from "../../data/actions";
  import horizontalIcon from "../../../../assets/icons/horizontal.svg?url";
  import verticalIcon from "../../../../assets/icons/vertical.svg?url";
  import selectIcon from "../../../../assets/icons/select.svg?url";
  import tempoIcon from "../../../../assets/icons/tempo.svg?url";
  import goldIcon from "../../../../assets/icons/gold.svg?url";

  export let step: Step;
  export let inline: boolean;

  $: description = controlDescriptions[step.control];

  $: controlIcon =
    step.control === Control.X
      ? { src: horizontalIcon, alt: "Horizontal" }
      : step.control === Control.Y
        ? { src: verticalIcon, alt: "Vertical" }
        : step.control === Control.SELECT
          ? { src: selectIcon, alt: "Select" }
          : step.control === Control.TEMPO
            ? { src: tempoIcon, alt: "Tempo" }
            : step.control === Control.PARAMETER ||
                step.control === Control.LOWER_PARAM ||
                step.control === Control.UPPER_PARAM
              ? { src: goldIcon, alt: "Gold knob" }
              : undefined;
</script>

{#if step.action === Action.MENU}
  <span class="target-icon" class:hidden={inline}>&nbsp;</span>
  <span class="target-title">{@html step.label}</span>
{:else if description.type === ControlType.none}
  <span class="target-icon font-bold text-[#f00]">INVALID</span>
{:else if controlIcon}
  <span class="target-icon flex items-center justify-center" class:hidden={inline}>
    <img
      src={controlIcon.src}
      alt={controlIcon.alt}
      class="control-icon-image"
    />
  </span>
  <span class="target-title">{@html step.label || description.title}</span>
{:else if description.type === ControlType.circleButton}
  <span class="target-icon text-[var(--sl-color-text)]" class:hidden={inline}
    ><CircleButton /></span
  >
  <span class="target-title uppercase">{@html description.title}</span>
{:else if description.type === ControlType.grid}
  <span
    class={"target-icon " +
      (step.control === Control.GRID_LIT
        ? "text-[var(--sl-color-green-high)]"
        : "text-[var(--sl-color-gray-4)]")}
    class:hidden={inline}
  >
    <FullGrid />
  </span>
  <span class="target-title">{@html step.label || description.title}</span>
{:else if description.type === ControlType.gridCol}
  <span
    class={"target-icon " + (description.color && `text-${description.color}`)}
    class:hidden={inline}
  >
    <GridCol />
  </span>
  <span class="target-title">{@html description.title}</span>
{:else if description.type === ControlType.blackKnob}
  <span class="target-icon text-[var(--sl-color-text)]" class:hidden={inline}
    ><Knob /></span
  >
  <span class="target-title uppercase">{@html description.title}</span>
{:else if description.type === ControlType.goldKnob}
  <span class="target-icon text-[var(--sl-color-accent)]" class:hidden={inline}
    ><Knob /></span
  >
  <span class="target-title uppercase">{@html description.title}</span>
{:else if description.type === ControlType.display}
  <span class="target-icon" class:hidden={inline}>&nbsp;</span>
  <span class="target-title rounded bg-[var(--sl-color-black)]/85 px-2 font-mono text-[var(--sl-color-white)]"
    >{step.label}</span
  >
  <span class="target-title font-mono uppercase">{@html description.title}</span
  >
{:else if description.type === ControlType.external}
  <span class="target-icon text-[var(--sl-color-gray-4)]" class:hidden={inline}
    ><Midi /></span
  >
  <span class="target-title italic">{@html description.title}</span>
{/if}

<style lang="postcss">
  .target-icon {
    grid-area: target-icon;
    line-height: 1;
  }
  .target-title {
    text-align: center;
    font-size: 0.75rem;
    line-height: 1;
    white-space: nowrap;
    grid-area: target-title;
  }

  .control-icon-image {
    height: 2.175em;
    width: auto;
    display: inline-block;
    vertical-align: middle;
    transform: translateY(0.12em);
  }
</style>
