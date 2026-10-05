<script setup lang="ts">
import { ref, computed, watch } from 'vue';
import { storeToRefs } from 'pinia';
import BridgeSelector from './BridgeSelector.vue';
import { useLearnStore } from '../stores/learn';
import { useBridgeStore } from '../stores/bridges';
import { useDeviceStore } from '../stores/devices';
import { useCommonStore } from '../stores/common';
import { api } from '../services/api';
import { useI18n } from '../i18n';
import type { IRCode, IRDevice } from '../types';

const props = defineProps({
    show: Boolean,
    device: {
        type: Object as () => IRDevice | null,
        default: null,
    },
});

const emit = defineEmits(['close']);

const learnStore = useLearnStore();
const bridgeStore = useBridgeStore();
const deviceStore = useDeviceStore();
const commonStore = useCommonStore();

const { learn } = storeToRefs(learnStore);
const { onlineBridges, hasOnlineBridges } = storeToRefs(bridgeStore);
const { t } = useI18n();

const remoteName = ref('');
const buttonLabel = ref('');
const deviceId = ref<string | null>(null);
const savedButtons = ref<{ name: string; code: IRCode }[]>([]);
const saving = ref(false);

// When a device is passed in we only add buttons to it, never create a new one.
const isExistingDevice = computed(() => !!props.device);
const targetDeviceId = computed(() => props.device?.id ?? deviceId.value);

// Prefer the device's own target bridge, then an RF-capable online bridge,
// otherwise the first online one.
const defaultBridgeId = computed(() => {
    const deviceBridge = props.device?.target_bridges?.[0];
    if (deviceBridge) return deviceBridge;
    const rfBridge = onlineBridges.value.find(b => b.rf_capable);
    return (rfBridge ?? onlineBridges.value[0])?.id ?? '';
});

const codeReady = computed(() => !!learn.value.last_code && learn.value.received_codes.length > 0);

const summary = computed(() => {
    const code = learn.value.last_code;
    if (!code) return null;
    const payload = code.payload ?? {};
    const timings = Array.isArray(payload.timings) ? (payload.timings as number[]) : null;
    const count = timings
        ? timings.length
        : (Array.isArray(payload.data) ? (payload.data as unknown[]).length : 0);
    const durationMs = timings
        ? Math.round(timings.reduce((acc, v) => acc + Math.abs(Number(v) || 0), 0) / 1000)
        : 0;
    return { protocol: code.protocol || 'raw', count, durationMs };
});

watch(() => props.show, (isOpen) => {
    if (isOpen) {
        // Reset per-open state so a different device never reuses stale values.
        remoteName.value = props.device?.name ?? '';
        deviceId.value = null;
        savedButtons.value = [];
        buttonLabel.value = '';
    }
    if (!isOpen || learn.value.active) return;
    // Default the source bridge to an online (preferably RF-capable) bridge.
    learn.value.targetBridges = defaultBridgeId.value ? [defaultBridgeId.value] : [];
});

const startLearning = () => {
    if (!learn.value.targetBridges.length && defaultBridgeId.value) {
        learn.value.targetBridges = [defaultBridgeId.value];
    }
    learnStore.startLearn();
};

const stopLearning = () => {
    learnStore.cancelLearn();
};

const close = () => {
    emit('close');
};

const saveAsButton = async () => {
    const code = learn.value.last_code;
    if (!code) return;

    if (!remoteName.value.trim()) {
        commonStore.addFlashMessage(t('rfRemote.needName'), 'error');
        return;
    }
    if (!buttonLabel.value.trim()) {
        commonStore.addFlashMessage(t('rfRemote.needLabel'), 'error');
        return;
    }

    const label = buttonLabel.value.trim();
    const cleanCode = JSON.parse(JSON.stringify(code)) as IRCode;
    const btnData = {
        name: label,
        icon: 'remote',
        code: cleanCode,
        is_output: true,
        is_input: false,
        is_event: true,
    };

    saving.value = true;
    try {
        const targetId = targetDeviceId.value;
        if (!targetId) {
            const device = await api<IRDevice>('devices', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    name: remoteName.value.trim(),
                    icon: 'remote-tv',
                    target_bridges: learn.value.targetBridges,
                    allowed_bridges: [],
                    buttons: [btnData],
                }),
            });
            if (!device) return;
            deviceId.value = device.id;
            commonStore.addFlashMessage(t('rfRemote.deviceCreated', { name: device.name }), 'success');
        } else {
            const created = await api(`devices/${targetId}/buttons`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(btnData),
            });
            if (!created) return;
            commonStore.addFlashMessage(t('rfRemote.buttonSaved', { name: label }), 'success');
        }

        savedButtons.value.push({ name: label, code: cleanCode });
        await deviceStore.fetchDevices();
        learnStore.consumeLearnedCode(code);
        buttonLabel.value = '';

        // Keep listening so the next key can be learned right away.
        learnStore.startLearn();
    } catch (e) {
        commonStore.addFlashMessage(t('rfRemote.saveFailed', { msg: (e as Error).message }), 'error');
    } finally {
        saving.value = false;
    }
};
</script>
<template>
  <div
    v-if="show"
    class="fixed inset-0 !m-0 bg-gray-900/60 flex items-center justify-center z-[60] backdrop-blur-sm"
    @click.self="close"
  >
    <div
      class="bg-gray-900 border border-gray-700 rounded-lg p-6 max-w-lg w-full max-h-[90vh] flex flex-col shadow-2xl animate-in fade-in scale-95 duration-200"
      style="animation: slideInUp 0.3s ease-out;"
    >
      <div class="flex justify-between items-center mb-4 shrink-0">
        <h2 class="text-lg font-semibold flex items-center gap-2">
          <i class="mdi mdi-radio-tower text-ha-500" />
          {{ isExistingDevice ? t('rfRemote.addButtonTitle') : t('rfRemote.title') }}
        </h2>
        <button
          class="text-gray-500 hover:text-gray-300 transition-colors"
          @click="close"
        >
          <i class="mdi mdi-close text-2xl" />
        </button>
      </div>

      <div class="space-y-4 overflow-y-auto min-h-0">
        <div>
          <label class="block text-sm font-medium text-gray-300 mb-1">{{ t('rfRemote.nameLabel') }}</label>
          <input
            v-model="remoteName"
            :placeholder="t('rfRemote.namePlaceholder')"
            class="w-full rounded p-2 text-sm"
            :disabled="!!targetDeviceId"
          >
        </div>

        <div>
          <BridgeSelector
            v-if="hasOnlineBridges"
            v-model="learn.targetBridges"
            :bridges="onlineBridges"
            type="source"
          />
          <select
            v-else
            class="p-2 rounded bg-gray-900 border border-gray-700 w-full"
            disabled
          >
            <option>{{ t('learn.noBridges') }}</option>
          </select>
          <p class="text-xs text-gray-400 mt-1">
            {{ t('rfRemote.selectBridgeDesc') }}
          </p>
        </div>

        <div class="flex items-center gap-3">
          <button
            class="btn btn-primary disabled:opacity-50 disabled:cursor-not-allowed"
            :disabled="!hasOnlineBridges || learn.active"
            @click="startLearning"
          >
            <i class="mdi mdi-radio-tower mr-1" />
            {{ t('rfRemote.start') }}
          </button>
          <button
            v-if="learn.active"
            class="btn btn-danger"
            @click="stopLearning"
          >
            <i class="mdi mdi-stop mr-1" />
            {{ t('rfRemote.stop') }}
          </button>
        </div>

        <div
          v-if="learn.active"
          class="p-4 rounded-lg border border-green-700 bg-green-900/20 text-center"
        >
          <i class="mdi mdi-access-point text-3xl text-green-400 pulse" />
          <p class="font-semibold text-green-400 mt-1">
            {{ t('rfRemote.waiting') }}
          </p>
          <p class="text-xs text-gray-400 mt-1">
            {{ t('rfRemote.pressRemote') }}
          </p>
        </div>

        <div
          v-if="codeReady && summary"
          class="p-4 rounded-lg border border-gray-700 bg-gray-800/50 space-y-3"
        >
          <div class="flex items-center gap-2 text-green-400 font-semibold text-sm">
            <i class="mdi mdi-check-circle-outline" />
            {{ t('rfRemote.newCode') }}
          </div>
          <div class="grid grid-cols-3 gap-2 text-center text-xs">
            <div class="p-2 rounded bg-gray-900 border border-gray-700">
              <div class="text-gray-500">{{ t('rfRemote.protocol') }}</div>
              <div class="font-bold text-gray-200 uppercase">{{ summary.protocol }}</div>
            </div>
            <div class="p-2 rounded bg-gray-900 border border-gray-700">
              <div class="text-gray-500">{{ t('rfRemote.timingsCount') }}</div>
              <div class="font-bold text-gray-200">{{ summary.count }}</div>
            </div>
            <div class="p-2 rounded bg-gray-900 border border-gray-700">
              <div class="text-gray-500">{{ t('rfRemote.duration') }}</div>
              <div class="font-bold text-gray-200">{{ summary.durationMs }} ms</div>
            </div>
          </div>
          <div>
            <label class="block text-sm font-medium text-gray-300 mb-1">{{ t('rfRemote.buttonLabel') }}</label>
            <input
              v-model="buttonLabel"
              :placeholder="t('rfRemote.buttonPlaceholder')"
              class="w-full rounded p-2 text-sm"
              @keyup.enter="saveAsButton"
            >
          </div>
          <button
            class="btn btn-primary w-full justify-center disabled:opacity-50 disabled:cursor-not-allowed"
            :disabled="saving || !buttonLabel.trim()"
            @click="saveAsButton"
          >
            <i class="mdi mdi-content-save mr-1" />
            {{ t('rfRemote.save') }}
          </button>
        </div>

        <div>
          <h4 class="text-sm font-semibold text-gray-300 mb-2">
            {{ t('rfRemote.savedButtons', { count: savedButtons.length }) }}
          </h4>
          <div
            v-if="savedButtons.length === 0"
            class="text-xs text-gray-500"
          >
            {{ t('rfRemote.noSavedButtons') }}
          </div>
          <div
            v-else
            class="flex flex-wrap gap-2"
          >
            <div
              v-for="(btn, idx) in savedButtons"
              :key="idx"
              class="flex items-center gap-1 text-xs bg-gray-800 px-2 py-1 rounded border border-gray-600 text-gray-300"
            >
              <i class="mdi mdi-remote" />
              <span class="font-medium">{{ btn.name }}</span>
              <span class="text-gray-500 uppercase">{{ btn.code.protocol }}</span>
            </div>
          </div>
        </div>
      </div>

      <div class="flex justify-end gap-4 mt-6 shrink-0">
        <button
          class="btn"
          @click="close"
        >
          {{ t('rfRemote.close') }}
        </button>
      </div>
    </div>
  </div>
</template>

<style scoped>
@keyframes slideInUp {
  from {
    opacity: 0;
    transform: translateY(20px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}
</style>
