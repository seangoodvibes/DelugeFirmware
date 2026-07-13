<script lang="ts">
  import { allViews } from "../stores/view_store.js";
  import { allFirmwares } from "../stores/firmware_store.js";
  import { shortcutGroups } from "../stores/group_store.js";
  import { shortcutControlGroups } from "../stores/control_store.js";
  import {
    availableControls,
    availableFirmwares,
    availableGroupIds,
    availableViews,
  } from "../stores/shortcut_store.js";
  import FirmwareFilterItem from "./FirmwareFilterItem.svelte";
  import GroupFilterItem from "./GroupFilterItem.svelte";
  import ControlFilterItem from "./ControlFilterItem.svelte";
  import ViewFilterItem from "./ViewFilterItem.svelte";
</script>

<div class="filter-panel my-4">
  <section>
    <details class="filter-collapsible" open>
      <summary class="filter-title">Firmware Filter</summary>
      <div class="filter-content flex flex-wrap items-center gap-2">
        {#each $allFirmwares as firmware}
          {#if $availableFirmwares.has(firmware.id)}
            <FirmwareFilterItem {firmware} />
          {/if}
        {/each}
      </div>
    </details>
  </section>

  <section>
    <details class="filter-collapsible" open>
      <summary class="filter-title">Group Filter</summary>
      <div class="filter-content flex flex-wrap items-center gap-2">
        {#each shortcutGroups as group}
          {#if $availableGroupIds.has(group.id)}
            <GroupFilterItem {group} />
          {/if}
        {/each}
      </div>
    </details>
  </section>

  <section>
    <details class="filter-collapsible" open>
      <summary class="filter-title">View Filter</summary>
      <div class="filter-content flex flex-wrap items-center gap-2">
        {#each $allViews as view}
          {#if $availableViews.has(view.id)}
            <ViewFilterItem {view} />
          {/if}
        {/each}
      </div>
    </details>
  </section>

  <section>
    <details class="filter-collapsible">
      <summary class="filter-title">Control Filter</summary>
      <div class="filter-content filter-groups">
        {#each shortcutControlGroups as controlGroup}
          <section class="filter-subgroup">
            <h3 class="filter-subtitle">{controlGroup.title}</h3>
            <div class="flex flex-wrap items-center gap-2">
              {#each controlGroup.controls as control}
                {#if $availableControls.has(control.id)}
                  <ControlFilterItem {control} />
                {/if}
              {/each}
            </div>
          </section>
        {/each}
      </div>
    </details>
  </section>
</div>

<style>
  .filter-panel {
    --dc-group-bg: rgb(83 98 120 / 0.3);
    --dc-group-border: rgb(178 205 222 / 0.72);
    --dc-group-fg: var(--sl-color-gray-1);

    display: flex;
    flex-direction: column;
    gap: 1rem;
  }

  :global(html[data-theme="light"]) .filter-panel {
    --dc-group-bg: rgb(226 232 240 / 0.9);
    --dc-group-border: rgb(106 148 187 / 0.85);
    --dc-group-fg: rgb(41 50 66);
  }

  .filter-title {
    margin: 0;
    font-size: 1.15rem;
    font-weight: 700;
    line-height: 1.2;
    display: flex;
    align-items: center;
  }

  .filter-collapsible > .filter-title {
    cursor: pointer;
    gap: 0.5rem;
    list-style: none;
  }

  .filter-collapsible > .filter-title::before {
    content: "▸";
    font-size: 0.9rem;
    line-height: 1;
    transform: translateY(-0.02rem);
  }

  .filter-collapsible[open] > .filter-title::before {
    content: "▾";
  }

  .filter-collapsible > .filter-title::-webkit-details-marker {
    display: none;
  }

  .filter-content {
    margin-top: 0.75rem;
  }

  .filter-groups {
    display: flex;
    flex-direction: column;
    gap: 0.875rem;
  }

  .filter-subgroup {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }

  .filter-subtitle {
    margin: 0;
    font-size: 0.95rem;
    font-weight: 600;
    line-height: 1.2;
    color: var(--sl-color-gray-2);
  }
</style>
