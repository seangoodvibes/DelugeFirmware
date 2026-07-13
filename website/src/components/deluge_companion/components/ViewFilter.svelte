<script lang="ts">
  import { allViews } from "../stores/view_store.js";
  import { shortcutGroups } from "../stores/group_store.js";
  import { shortcutControlGroups } from "../stores/control_store.js";
  import GroupFilterItem from "./GroupFilterItem.svelte";
  import ControlFilterItem from "./ControlFilterItem.svelte";
  import ViewFilterItem from "./ViewFilterItem.svelte";
</script>

<div class="filter-panel my-4">
  <section>
    <h2 class="filter-title">Group Filter:</h2>
    <div class="flex flex-wrap items-center gap-2">
      {#each shortcutGroups as group}
        <GroupFilterItem {group} />
      {/each}
    </div>
  </section>

  <section>
    <h2 class="filter-title">Tag Filter:</h2>
    <div class="flex flex-wrap items-center gap-2">
      {#each $allViews as view}
        <ViewFilterItem {view} />
      {/each}
    </div>
  </section>

  <section>
    <h2 class="filter-title">Control Filter:</h2>
    <div class="filter-groups">
      {#each shortcutControlGroups as controlGroup}
        <section class="filter-subgroup">
          <h3 class="filter-subtitle">{controlGroup.title}</h3>
          <div class="flex flex-wrap items-center gap-2">
            {#each controlGroup.controls as control}
              <ControlFilterItem {control} />
            {/each}
          </div>
        </section>
      {/each}
    </div>
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
    margin: 0 0 0.75rem 0;
    font-size: 1.15rem;
    font-weight: 700;
    line-height: 1.2;
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
