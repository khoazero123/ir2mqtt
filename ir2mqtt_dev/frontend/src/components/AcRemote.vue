<script setup lang="ts">
import { ref, computed, onMounted, watch } from 'vue';
import { api } from '../services/api';
import { useDeviceStore } from '../stores/devices';
import { useI18n } from '../i18n';
import type { Device } from '../stores/devices';

const props = defineProps<{
    device: Device;
    canSend: boolean;
}>();

const deviceStore = useDeviceStore();
const { t } = useI18n();

const AC_PROTOCOL = 'kookong_ac';

interface AcState {
    power: number;
    mode: number;
    temperature: number;
    wind_speed: number;
    lr_wind_mode: number;
    ud_wind_mode: number;
}

const state = ref<AcState>({ power: 1, mode: 1, temperature: 25, wind_speed: 0, lr_wind_mode: 0, ud_wind_mode: 0 });
const busy = ref(false);

const MODE_LABELS = ['Auto', 'Cool', 'Heat', 'Fan', 'Dry'];
const WIND_LABELS = ['Auto', 'Low', 'Medium', 'High'];

const modeLabel = computed(() => MODE_LABELS[state.value.mode] ?? state.value.mode);
const windLabel = computed(() => WIND_LABELS[state.value.wind_speed] ?? state.value.wind_speed);
const isOff = computed(() => state.value.power === 0);

const actionButtons = computed(() =>
    props.device.buttons.filter(b => b.code?.protocol === AC_PROTOCOL),
);

const buttonFor = (action: string) =>
    actionButtons.value.find(b => (b.code?.payload as { action?: string })?.action === action);

const fetchState = async () => {
    try {
        const data = await api<AcState>(`match/ac_state?device_id=${props.device.id}`);
        if (data) state.value = data;
    } catch {
        /* keep the last known state */
    }
};

const press = async (action: string) => {
    const btn = buttonFor(action);
    if (!btn || busy.value || !props.canSend) return;
    busy.value = true;
    try {
        await deviceStore.triggerButton(props.device.id, btn.id);
        await fetchState();
    } finally {
        busy.value = false;
    }
};

onMounted(fetchState);
watch(() => props.device.id, fetchState);
</script>

<template>
  <div
    class="p-4 bg-gray-900 space-y-4"
    data-tour-id="ac-remote"
  >
    <!-- status bar -->
    <div class="flex items-center justify-between rounded-lg bg-gray-800 border border-gray-700 px-4 py-3">
      <div class="flex items-center gap-3">
        <i
          class="mdi text-2xl"
          :class="isOff ? 'mdi-power-off text-gray-500' : 'mdi-snowflake text-cyan-400'"
        />
        <div>
          <div
            class="text-lg font-medium leading-tight"
            :class="isOff ? 'text-gray-500' : 'text-gray-100'"
          >
            {{ isOff ? t('ac.off') : `${state.temperature}°C` }}
          </div>
          <div class="text-xs text-gray-400">
            {{ isOff ? t('ac.standby') : `${modeLabel} · ${t('ac.fan')} ${windLabel} · ${state.ud_wind_mode ? t('ac.swingOn') : t('ac.swingOff')}` }}
          </div>
        </div>
      </div>
      <button
        class="btn btn-sm"
        :class="isOff ? 'btn-secondary' : 'btn-primary'"
        :disabled="busy || !canSend"
        :title="t('ac.power')"
        @click="press('power')"
      >
        <i class="mdi mdi-power text-lg" />
      </button>
    </div>

    <!-- temperature -->
    <div class="flex items-center justify-center gap-4">
      <button
        class="w-16 h-16 rounded-full bg-gray-800 border border-gray-600 hover:bg-gray-700 flex items-center justify-center text-2xl text-gray-200 disabled:opacity-40"
        :disabled="busy || !canSend || isOff"
        :title="t('ac.tempDown')"
        @click="press('temp_down')"
      >
        <i class="mdi mdi-minus" />
      </button>
      <div class="w-28 text-center">
        <div class="text-4xl font-light text-gray-100">
          {{ state.temperature }}<span class="text-xl text-gray-400">°C</span>
        </div>
        <div class="text-xs text-gray-500">
          {{ modeLabel }}
        </div>
      </div>
      <button
        class="w-16 h-16 rounded-full bg-gray-800 border border-gray-600 hover:bg-gray-700 flex items-center justify-center text-2xl text-gray-200 disabled:opacity-40"
        :disabled="busy || !canSend || isOff"
        :title="t('ac.tempUp')"
        @click="press('temp_up')"
      >
        <i class="mdi mdi-plus" />
      </button>
    </div>

    <!-- mode / fan / swing -->
    <div class="grid grid-cols-3 gap-2">
      <button
        class="btn btn-secondary btn-sm flex-col !h-auto py-3 gap-1"
        :disabled="busy || !canSend"
        :title="t('ac.mode')"
        @click="press('mode')"
      >
        <i class="mdi mdi-tune text-lg" />
        <span class="text-xs">{{ t('ac.mode') }}</span>
        <span class="text-[10px] text-gray-400">{{ modeLabel }}</span>
      </button>
      <button
        class="btn btn-secondary btn-sm flex-col !h-auto py-3 gap-1"
        :disabled="busy || !canSend"
        :title="t('ac.fan')"
        @click="press('fan')"
      >
        <i class="mdi mdi-fan text-lg" />
        <span class="text-xs">{{ t('ac.fan') }}</span>
        <span class="text-[10px] text-gray-400">{{ windLabel }}</span>
      </button>
      <button
        class="btn btn-secondary btn-sm flex-col !h-auto py-3 gap-1"
        :disabled="busy || !canSend"
        :title="t('ac.swing')"
        @click="press('swing')"
      >
        <i class="mdi mdi-arrow-oscillating text-lg" />
        <span class="text-xs">{{ t('ac.swing') }}</span>
        <span class="text-[10px] text-gray-400">{{ state.ud_wind_mode ? t('ac.on') : t('ac.off') }}</span>
      </button>
    </div>
  </div>
</template>
