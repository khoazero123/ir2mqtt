<script setup lang="ts">
import { ref, computed, onMounted } from 'vue';
import { storeToRefs } from 'pinia';
import BridgeSelector from '../components/BridgeSelector.vue';
import { useMatchStore } from '../stores/match';
import { useBridgeStore } from '../stores/bridges';
import { useDeviceStore } from '../stores/devices';
import { useCommonStore } from '../stores/common';
import { useI18n } from '../i18n';
import type { MatchBrand, MatchCandidate, MatchAcState } from '../stores/match';

const matchStore = useMatchStore();
const bridgeStore = useBridgeStore();
const deviceStore = useDeviceStore();
const commonStore = useCommonStore();
const { t } = useI18n();

const { categories, brands, candidates, available } = storeToRefs(matchStore);
const { onlineBridges, hasOnlineBridges } = storeToRefs(bridgeStore);

type Step = 'category' | 'brand' | 'match' | 'done';
const step = ref<Step>('category');

const selectedCategory = ref<{ id: number; name: string } | null>(null);
const selectedBrand = ref<MatchBrand | null>(null);
const index = ref(0);
const sendTargets = ref<string[]>([]);
const brandQuery = ref('');
const countryOptions = ref<string[]>([]);
const selectedCountry = ref<string | null>(null);
const busy = ref(false);
const lastSent = ref<string | null>(null);
const saveName = ref('');
const savedDeviceName = ref<string | null>(null);

// AC test state — a tiny virtual remote, exactly like the phone app's match screen
// NOTE: bắt đầu ở power=0 (OFF) để lần bấm Power ĐẦU TIÊN gửi frame BẬT máy
// (trước đây mặc định power=1 nên lần bấm đầu lại gửi "Power Off" — dễ gây nhầm).
const acState = ref<MatchAcState>({
    power: 0,
    mode: 1,
    temperature: 25,
    wind_speed: 0,
    lr_wind_mode: 0,
    ud_wind_mode: 0,
});

const acModeLabels = ['Auto', 'Cool', 'Heat', 'Fan', 'Dry'];
const acWindLabels = ['Auto', 'Low', 'Medium', 'High'];

const acStateLabel = computed(() => {
    const s = acState.value;
    const swing = s.ud_wind_mode ? t('match.swingOn') : t('match.swingOff');
    return `${s.power ? 'ON' : 'OFF'} · ${acModeLabels[s.mode] ?? s.mode} · ${s.temperature}°C · ${t('match.fan')} ${acWindLabels[s.wind_speed] ?? s.wind_speed} · ${swing}`;
});

const resetAcState = () => {
    acState.value = { power: 0, mode: 1, temperature: 25, wind_speed: 0, lr_wind_mode: 0, ud_wind_mode: 0 };
};

// Air Conditioner first, then the rest by size
const orderedCategories = computed(() => {
    const list = [...categories.value];
    return list.sort((a, b) => {
        if (a.name === 'Air Conditioner') return -1;
        if (b.name === 'Air Conditioner') return 1;
        return b.count - a.count || a.id - b.id;
    });
});

const filteredBrands = computed(() => {
    const q = brandQuery.value.trim().toLowerCase();
    if (!q) return brands.value;
    return brands.value.filter(b => b.name.toLowerCase().includes(q));
});

const current = computed<MatchCandidate | null>(() => candidates.value[index.value] ?? null);
const total = computed(() => candidates.value.length);
const progress = computed(() => (total.value ? Math.round(((index.value + 1) / total.value) * 100) : 0));

onMounted(async () => {
    const ok = await matchStore.fetchAvailable();
    if (ok) await matchStore.fetchCategories();
});

const pickCategory = async (cat: { id: number; name: string }) => {
    busy.value = true;
    selectedCategory.value = cat;
    brandQuery.value = '';
    await matchStore.fetchBrands(cat.id);
    busy.value = false;
    step.value = 'brand';
};

const pickBrand = async (brand: MatchBrand) => {
    if (!selectedCategory.value) return;
    busy.value = true;
    selectedBrand.value = brand;
    const countries = await matchStore.fetchCountries(selectedCategory.value.id, brand.id);
    countryOptions.value = countries?.countries ?? [];
    selectedCountry.value = countries?.recommended ?? countryOptions.value[0] ?? null;
    await matchStore.fetchCandidates(
        selectedCategory.value.id,
        brand.id,
        selectedCountry.value,
    );
    index.value = 0;
    lastSent.value = null;
    resetAcState();
    saveName.value = `${brand.name} ${selectedCategory.value.name}`;
    busy.value = false;
    step.value = 'match';
};

/** Re-fetch the candidate list when the selected region changes. */
const changeCountry = async () => {
    if (!selectedCategory.value || !selectedBrand.value) return;
    busy.value = true;
    try {
        await matchStore.fetchCandidates(
            selectedCategory.value.id,
            selectedBrand.value.id,
            selectedCountry.value,
        );
        index.value = 0;
        lastSent.value = null;
        resetAcState();
    } finally {
        busy.value = false;
    }
};

const back = () => {
    if (step.value === 'match') { step.value = 'brand'; return; }
    if (step.value === 'brand') { step.value = 'category'; return; }
    if (step.value === 'done') { step.value = 'match'; }
};

const sendTest = async () => {
    const cand = current.value;
    if (!cand || !selectedCategory.value || !selectedBrand.value) return;
    busy.value = true;
    lastSent.value = null;
    try {
        const res = await matchStore.testRemote(
            selectedCategory.value.id,
            selectedBrand.value.id,
            cand.remote_id,
            sendTargets.value,
            cand.is_ac ? { ...acState.value, function_id: 1 } : undefined,
        );
        if (res?.sent) {
            lastSent.value = res.button_name;
            commonStore.addFlashMessage(t('match.sent', { button: res.button_name }), 'success', 3000);
        }
    } finally {
        busy.value = false;
    }
};

/** Press one AC key on the virtual remote: mutate state, then transmit. */
const sendAcKey = async (key: string) => {
    const cand = current.value;
    if (!cand || !selectedCategory.value || !selectedBrand.value) return;
    const s: MatchAcState = { ...acState.value };
    let fid = 1;
    switch (key) {
        case 'power':
            fid = 1;
            s.power = s.power ? 0 : 1;
            break;
        case 'temp_up':
            fid = 3;
            s.temperature = Math.min(30, s.temperature + 1);
            break;
        case 'temp_down':
            fid = 4;
            s.temperature = Math.max(16, s.temperature - 1);
            break;
        case 'mode':
            fid = 2;
            s.mode = (s.mode + 1) % 5;
            break;
        case 'wind':
            fid = 5;
            s.wind_speed = (s.wind_speed + 1) % 4;
            break;
        case 'swing':
            s.ud_wind_mode = s.ud_wind_mode ? 0 : 1;
            fid = s.ud_wind_mode ? 6 : 7;
            break;
        default:
            return;
    }
    acState.value = s;

    busy.value = true;
    lastSent.value = null;
    try {
        const res = await matchStore.testRemote(
            selectedCategory.value.id,
            selectedBrand.value.id,
            cand.remote_id,
            sendTargets.value,
            { ...s, function_id: fid },
        );
        if (res?.sent) {
            lastSent.value = res.button_name;
            commonStore.addFlashMessage(t('match.sent', { button: res.button_name }), 'success', 3000);
        }
    } finally {
        busy.value = false;
    }
};

const next = () => {
    if (index.value < total.value - 1) index.value += 1;
    lastSent.value = null;
    resetAcState();
};

const prev = () => {
    if (index.value > 0) index.value -= 1;
    lastSent.value = null;
    resetAcState();
};

const saveMatched = async () => {
    const cand = current.value;
    if (!cand || !selectedCategory.value || !selectedBrand.value) return;
    busy.value = true;
    try {
        const dev = await matchStore.saveRemote(
            selectedCategory.value.id,
            selectedBrand.value.id,
            cand.remote_id,
            saveName.value || `${selectedBrand.value.name} ${selectedCategory.value.name}`,
            sendTargets.value,
        ) as { name?: string } | null;
        savedDeviceName.value = dev?.name ?? saveName.value;
        await deviceStore.fetchDevices?.();
        step.value = 'done';
    } finally {
        busy.value = false;
    }
};

const restart = async () => {
    step.value = 'category';
    selectedCategory.value = null;
    selectedBrand.value = null;
    savedDeviceName.value = null;
    await matchStore.fetchCategories();
};
</script>

<template>
  <div class="space-y-5">
    <p
      v-if="available === false"
      class="text-sm text-yellow-400 bg-yellow-900/20 border border-yellow-700/50 rounded p-3"
    >
      {{ t('match.unavailable') }}
    </p>

    <!-- STEP 1: category -->
    <template v-else-if="step === 'category'">
      <p class="text-sm text-gray-400">
        {{ t('match.introCategory') }}
      </p>
      <div class="grid grid-cols-2 sm:grid-cols-3 lg:grid-cols-4 gap-3">
        <button
          v-for="cat in orderedCategories"
          :key="cat.id"
          class="bg-gray-800 hover:bg-gray-700 border border-gray-600 rounded-lg p-4 text-left transition-colors"
          :data-tour-id="'match-cat-' + cat.id"
          @click="pickCategory(cat)"
        >
          <div class="flex items-center gap-2">
            <i
              class="mdi text-2xl text-ha-500"
              :class="cat.name === 'Air Conditioner' ? 'mdi-air-conditioner' : 'mdi-remote'"
            />
            <span class="font-medium">{{ cat.name }}</span>
          </div>
          <div class="text-xs text-gray-500 mt-1">
            {{ t('match.remotesCount', { count: cat.count }) }}
          </div>
        </button>
      </div>
    </template>

    <!-- STEP 2: brand -->
    <template v-else-if="step === 'brand'">
      <div class="flex items-center gap-3">
        <button
          class="btn btn-secondary btn-sm"
          @click="back"
        >
          <i class="mdi mdi-arrow-left" /> {{ t('match.back') }}
        </button>
        <div class="text-sm text-gray-400">
          {{ selectedCategory?.name }} · {{ t('match.brandsCount', { count: brands.length }) }}
        </div>
      </div>

      <input
        v-model="brandQuery"
        type="text"
        class="input w-full max-w-sm"
        :placeholder="t('match.searchBrand')"
      >

      <div class="grid grid-cols-2 sm:grid-cols-3 lg:grid-cols-4 gap-2 max-h-[60vh] overflow-auto pr-1">
        <button
          v-for="brand in filteredBrands"
          :key="brand.id"
          class="bg-gray-800 hover:bg-gray-700 border border-gray-600 rounded px-3 py-2 text-left text-sm transition-colors"
          @click="pickBrand(brand)"
        >
          <div class="truncate">{{ brand.name }}</div>
          <div class="text-xs text-gray-500">
            {{ brand.count }}
          </div>
        </button>
      </div>
      <p
        v-if="!filteredBrands.length"
        class="text-sm text-gray-500 italic"
      >
        {{ t('match.noBrands') }}
      </p>
    </template>

    <!-- STEP 3: match -->
    <template v-else-if="step === 'match'">
      <div class="flex flex-wrap items-center gap-3">
        <button
          class="btn btn-secondary btn-sm"
          @click="back"
        >
          <i class="mdi mdi-arrow-left" /> {{ t('match.back') }}
        </button>
        <div class="text-sm text-gray-300">
          <span class="font-medium">{{ selectedBrand?.name }}</span>
          <span class="text-gray-500"> · {{ selectedCategory?.name }}</span>
        </div>
        <div class="ml-auto text-sm text-gray-400">
          {{ t('match.position', { index: index + 1, total }) }}
        </div>
      </div>

      <!-- region / country selector — mirrors the phone app's region list -->
      <div
        v-if="countryOptions.length"
        class="flex flex-wrap items-center gap-2 text-sm"
      >
        <label class="text-gray-400"><i class="mdi mdi-earth" /> {{ t('match.region') }}</label>
        <select
          v-model="selectedCountry"
          class="input py-1"
          :disabled="busy"
          @change="changeCountry"
        >
          <option
            v-for="c in countryOptions"
            :key="c"
            :value="c"
          >
            {{ c }}
          </option>
        </select>
        <span class="text-xs text-gray-500">{{ t('match.regionHint') }}</span>
      </div>

      <div class="h-1.5 bg-gray-700 rounded overflow-hidden">
        <div
          class="h-full bg-ha-500 transition-all"
          :style="{ width: progress + '%' }"
        />
      </div>

      <!-- target bridge -->
      <div class="bg-gray-800 border border-gray-600 rounded-lg p-3">
        <div class="text-xs text-gray-400 mb-2">
          <i class="mdi mdi-access-point" /> {{ t('match.target') }}
        </div>
        <BridgeSelector
          v-if="hasOnlineBridges"
          v-model="sendTargets"
          :bridges="onlineBridges"
          type="target"
          :compact="true"
        />
        <p
          v-else
          class="text-xs text-red-400 italic"
        >
          {{ t('learn.noBridges') }}
        </p>
      </div>

      <!-- current candidate -->
      <div
        v-if="current"
        class="bg-gray-800 border border-gray-600 rounded-lg p-5 space-y-4"
      >
        <div class="flex items-center gap-3">
          <i
            class="mdi text-3xl text-ha-500"
            :class="current.is_ac ? 'mdi-air-conditioner' : 'mdi-remote'"
          />
          <div>
            <div class="font-medium">{{ current.label }}</div>
            <div class="text-xs text-gray-500">
              {{ t('match.remoteMeta', { freq: current.frequency, rank: current.rank }) }}
              <span
                v-if="current.country"
                class="ml-2 px-1.5 py-0.5 rounded bg-gray-700 text-gray-300"
              >#{{ current.rank }} · {{ current.country }}</span>
            </div>
          </div>
        </div>

        <!-- AC virtual remote: press individual keys, like the phone app -->
        <div
          v-if="current.is_ac"
          class="space-y-3"
        >
          <div class="text-sm text-gray-200 bg-gray-900/60 border border-gray-700 rounded px-3 py-2 flex items-center gap-2">
            <i class="mdi mdi-air-conditioner text-ha-500" />
            <span>{{ acStateLabel }}</span>
          </div>
          <div class="grid grid-cols-3 sm:grid-cols-6 gap-2">
            <button
              class="btn btn-secondary btn-sm"
              :disabled="busy || !hasOnlineBridges"
              :title="acState.power ? 'Gui frame TAT may (Power Off)' : 'Gui frame BAT may (Power On)'"
              @click="sendAcKey('power')"
            >
              <i class="mdi mdi-power" /> {{ t('match.keyPower') }} → {{ acState.power ? 'OFF' : 'ON' }}
            </button>
            <button
              class="btn btn-secondary btn-sm"
              :disabled="busy || !hasOnlineBridges"
              @click="sendAcKey('temp_down')"
            >
              <i class="mdi mdi-thermometer-minus" /> {{ t('match.keyTempDown') }}
            </button>
            <button
              class="btn btn-secondary btn-sm"
              :disabled="busy || !hasOnlineBridges"
              @click="sendAcKey('temp_up')"
            >
              <i class="mdi mdi-thermometer-plus" /> {{ t('match.keyTempUp') }}
            </button>
            <button
              class="btn btn-secondary btn-sm"
              :disabled="busy || !hasOnlineBridges"
              @click="sendAcKey('mode')"
            >
              <i class="mdi mdi-tune" /> {{ t('match.keyMode') }}
            </button>
            <button
              class="btn btn-secondary btn-sm"
              :disabled="busy || !hasOnlineBridges"
              @click="sendAcKey('wind')"
            >
              <i class="mdi mdi-fan" /> {{ t('match.keyFan') }}
            </button>
            <button
              class="btn btn-secondary btn-sm"
              :disabled="busy || !hasOnlineBridges"
              @click="sendAcKey('swing')"
            >
              <i class="mdi mdi-arrow-oscillating" /> {{ t('match.keySwing') }}
            </button>
          </div>
          <span class="text-xs text-gray-500">{{ t('match.acHint') }}</span>
        </div>

        <div class="flex flex-wrap items-center gap-2">
          <button
            v-if="!current.is_ac"
            class="btn btn-primary"
            :disabled="busy || !hasOnlineBridges"
            @click="sendTest"
          >
            <i class="mdi mdi-send" /> {{ t('match.test') }}
          </button>
          <span
            v-if="lastSent"
            class="text-xs text-green-400"
          >
            <i class="mdi mdi-check-circle-outline" /> {{ t('match.lastSent', { button: lastSent }) }}
          </span>
        </div>

        <div class="border-t border-gray-700 pt-4 space-y-2">
          <p class="text-sm text-gray-300">
            {{ t('match.didItWork') }}
          </p>
          <div class="flex flex-wrap gap-2">
            <button
              class="btn btn-primary"
              :disabled="busy"
              @click="saveMatched"
            >
              <i class="mdi mdi-check" /> {{ t('match.worked') }}
            </button>
            <button
              class="btn btn-secondary"
              :disabled="busy || index >= total - 1"
              @click="next"
            >
              <i class="mdi mdi-close" /> {{ t('match.notWorked') }}
            </button>
            <div class="ml-auto flex gap-2">
              <button
                class="btn btn-secondary btn-sm"
                :disabled="index === 0"
                @click="prev"
              >
                <i class="mdi mdi-chevron-left" /> {{ t('match.previous') }}
              </button>
              <button
                class="btn btn-secondary btn-sm"
                :disabled="index >= total - 1"
                @click="next"
              >
                {{ t('match.skip') }} <i class="mdi mdi-chevron-right" />
              </button>
            </div>
          </div>
        </div>
      </div>
      <p
        v-else
        class="text-sm text-gray-500 italic"
      >
        {{ t('match.noCandidates') }}
      </p>
    </template>

    <!-- STEP 4: saved -->
    <template v-else>
      <div class="bg-gray-800 border border-gray-600 rounded-lg p-6 space-y-4 max-w-xl">
        <div class="flex items-center gap-3 text-green-400">
          <i class="mdi mdi-check-circle text-3xl" />
          <div>
            <div class="font-medium text-base">
              {{ t('match.savedTitle') }}
            </div>
            <div class="text-sm text-gray-400">
              {{ savedDeviceName }}
            </div>
          </div>
        </div>
        <p class="text-sm text-gray-400">
          {{ t('match.savedHint') }}
        </p>
        <div class="flex gap-2">
          <button
            class="btn btn-secondary"
            @click="restart"
          >
            <i class="mdi mdi-restart" /> {{ t('match.matchAnother') }}
          </button>
        </div>
      </div>
    </template>

    <!-- save name inline -->
    <div
      v-if="step === 'match' && current"
      class="bg-gray-800/60 border border-gray-700 rounded-lg p-3 flex flex-wrap items-center gap-3"
    >
      <label class="text-sm text-gray-400">{{ t('match.deviceName') }}</label>
      <input
        v-model="saveName"
        type="text"
        class="input flex-1 min-w-48"
      >
    </div>
  </div>
</template>
