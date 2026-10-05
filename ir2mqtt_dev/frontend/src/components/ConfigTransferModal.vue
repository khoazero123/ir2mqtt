<script setup lang="ts">
import { ref, watch } from 'vue';
import TreeView from './TreeView.vue';
import { useCommonStore } from '../stores/common';
import { api } from '../services/api';
import { startConfigTransferTour } from '../tour';
import { formatCodeDetailsString } from '../utils';
import { useI18n } from '../i18n';
import type {
    IRDevice, IRButton, IRAutomation,
} from '../types';

interface Config {
    devices: IRDevice[];
    automations: IRAutomation[];
}

interface BackupMeta {
    id: string;
    filename: string;
    created_at: string;
    size: number;
    device_count: number;
    automation_count: number;
}

interface TreeItem {
    id: string;
    name: string;
    icon: string;
    selected: boolean;
    isOpen: boolean;
    children?: TreeItem[];
    details?: string;
    indeterminate?: boolean;
    textClass?: string;
    data?: IRDevice | IRAutomation | IRButton;
    type?: string;
}

const props = defineProps({
    show: Boolean,
});

const emit = defineEmits(['close']);

const commonStore = useCommonStore();

const { t } = useI18n();

const mode = ref('export'); // 'import', 'export' or 'backups'
const importMode = ref<'replace' | 'merge'>('replace');
const configTree = ref<TreeItem[]>([]);
const importTree = ref<TreeItem[]>([]);
const rawConfig = ref<Config | null>(null);
const importData = ref<Config | null>(null);

const backups = ref<BackupMeta[]>([]);
const loadingBackups = ref(false);
const creatingBackup = ref(false);
const restoringId = ref<string | null>(null);
const restoreMode = ref<'replace' | 'merge'>('merge');

const fetchConfig = async () => {
    const config = await api<Config>('config/export');
    if (!config) return;
    rawConfig.value = config;
    configTree.value = transformConfigToTree(config);
};

const transformConfigToTree = (config: Config): TreeItem[] => {
    const tree: TreeItem[] = [
        {
            id: 'devices',
            name: 'Devices',
            icon: 'remote-tv',
            selected: true,
            isOpen: true,
            children: config.devices.map((device: IRDevice) => ({
                id: device.id,
                name: device.name,
                icon: device.icon,
                details: `${device.buttons.length} buttons`,
                selected: true,
                isOpen: false,
                children: device.buttons.map((button: IRButton) => ({
                    id: button.id,
                    name: button.name,
                    icon: button.icon || 'gesture-tap-button',
                    details: formatCodeDetailsString(button.code),
                    selected: true,
                    isOpen: false,
                })),
            })),
        },
        {
            id: 'automations',
            name: 'Automations',
            icon: 'robot',
            selected: true,
            isOpen: true,
            children: config.automations.map((automation: IRAutomation) => ({
                id: automation.id || '',
                name: automation.name,
                icon: 'robot',
                selected: true,
                isOpen: false,
            })),
        },
    ];
    return tree;
};

const getSelectedConfig = (): Config => {
    const selected: Config = {
        devices: [],
        automations: [],
    };

    const deviceTree = configTree.value.find((item) => item.id === 'devices');
    if (deviceTree && (deviceTree.selected || deviceTree.indeterminate)) {
        deviceTree.children?.forEach((deviceItem: TreeItem) => {
            if (deviceItem.selected || deviceItem.indeterminate) {
                const deviceConfig = rawConfig.value?.devices.find((d: IRDevice) => d.id === deviceItem.id);
                if (!deviceConfig) return;

                const selectedDevice: IRDevice = { ...deviceConfig, buttons: [] as IRButton[] };

                deviceItem.children?.forEach((buttonItem: TreeItem) => {
                    if (buttonItem.selected) {
                        const buttonConfig = deviceConfig.buttons.find((b: IRButton) => b.id === buttonItem.id);
                        if (buttonConfig) {
                            selectedDevice.buttons.push(buttonConfig);
                        }
                    }
                });
                selected.devices.push(selectedDevice);
            }
        });
    }

    const automationTree = configTree.value.find((item) => item.id === 'automations');
    if (automationTree && (automationTree.selected || automationTree.indeterminate)) {
        automationTree.children?.forEach((automationItem: TreeItem) => {
            if (automationItem.selected || automationItem.indeterminate) {
                const automationConfig = rawConfig.value?.automations.find((a: IRAutomation) => a.id === automationItem.id);
                if (automationConfig) {
                    selected.automations.push(automationConfig);
                }
            }
        });
    }

    return selected;
};

const downloadConfig = () => {
    const selectedConfig = getSelectedConfig();
    const dataStr = `data:text/json;charset=utf-8,${encodeURIComponent(JSON.stringify(selectedConfig, null, 2))}`;
    const downloadAnchorNode = document.createElement('a');
    downloadAnchorNode.setAttribute('href', dataStr);
    downloadAnchorNode.setAttribute('download', 'ir2mqtt_config_partial.json');
    document.body.appendChild(downloadAnchorNode);
    downloadAnchorNode.click();
    downloadAnchorNode.remove();
};

const handleFileSelect = (event: Event) => {
    const target = event.target as HTMLInputElement;
    const file = target.files?.[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = (e) => {
        try {
            const result = e.target?.result;
            if (typeof result === 'string') {
                importData.value = JSON.parse(result);
                generateImportTree();
            }
        } catch {
            commonStore.addFlashMessage(t('store.invalidJson'), 'error');
        }
    };
    reader.readAsText(file);
};

const isDeepEqual = (a: unknown, b: unknown) => JSON.stringify(a) === JSON.stringify(b);

const generateImportTree = () => {
    if (!importData.value || !rawConfig.value) return;

    const tree: TreeItem[] = [];
    const current = rawConfig.value;
    const imported = importData.value;

    const impDevices = imported.devices || [];
    const impAutos = imported.automations || [];
    const curDevices = current.devices || [];
    const curAutos = current.automations || [];

    // ─── Duplicate ID detection within the imported file ─────────────────
    const seenDevIds = new Set<string>();
    const duplicateDevIds = new Set<string>();
    impDevices.forEach((d: IRDevice) => {
        if (seenDevIds.has(d.id)) duplicateDevIds.add(d.id);
        else seenDevIds.add(d.id);
    });

    const seenAutoIds = new Set<string>();
    const duplicateAutoIds = new Set<string>();
    impAutos.forEach((a: IRAutomation) => {
        const id = a.id || '';
        if (!id) return;
        if (seenAutoIds.has(id)) duplicateAutoIds.add(id);
        else seenAutoIds.add(id);
    });

    // ─── Devices ─────────────────────────────────────────────────────────
    const devicesNode: TreeItem = {
        id: 'devices',
        name: 'Devices',
        icon: 'remote-tv',
        children: [],
        isOpen: true,
        selected: false,
    };

    impDevices.forEach((impDev: IRDevice) => {
        // Duplicate device ID within the import file
        if (duplicateDevIds.has(impDev.id)) {
            devicesNode.children?.push({
                id: impDev.id,
                name: impDev.name,
                icon: impDev.icon,
                details: t('configTransfer.status.duplicateId'),
                textClass: 'text-red-400',
                selected: false,
                isOpen: false,
                data: impDev,
                type: 'device',
            });
            return;
        }

        const currDev = curDevices.find((d: IRDevice) => d.id === impDev.id);
        let devStatus = 'same';
        const btnChildren: TreeItem[] = [];

        if (!currDev) {
            devStatus = 'new';
        } else if (impDev.name !== currDev.name || impDev.icon !== currDev.icon || !isDeepEqual(impDev.target_bridges, currDev.target_bridges)) {
            devStatus = 'changed';
        }

        // Buttons — detect duplicate IDs within this device
        if (impDev.buttons) {
            const seenBtnIds = new Set<string>();
            const duplicateBtnIds = new Set<string>();
            impDev.buttons.forEach((b: IRButton) => {
                if (seenBtnIds.has(b.id)) duplicateBtnIds.add(b.id);
                else seenBtnIds.add(b.id);
            });

            impDev.buttons.forEach((impBtn: IRButton) => {
                if (duplicateBtnIds.has(impBtn.id)) {
                    btnChildren.push({
                        id: impBtn.id,
                        name: impBtn.name,
                        icon: impBtn.icon || 'gesture-tap-button',
                        details: t('configTransfer.status.duplicateId'),
                        textClass: 'text-red-400',
                        selected: false,
                        isOpen: false,
                        data: impBtn,
                        type: 'button',
                    });
                    return;
                }

                const currBtn = currDev ? currDev.buttons.find((b: IRButton) => b.id === impBtn.id) : null;
                let btnStatus = 'same';
                if (!currBtn) btnStatus = 'new';
                else if (!isDeepEqual(impBtn, currBtn)) btnStatus = 'changed';

                if (btnStatus !== 'same') {
                    btnChildren.push({
                        id: impBtn.id,
                        name: impBtn.name,
                        icon: impBtn.icon || 'gesture-tap-button',
                        details: t('configTransfer.status.' + btnStatus),
                        textClass: btnStatus === 'new' ? 'text-green-400' : 'text-orange-400',
                        selected: true,
                        isOpen: false,
                        data: impBtn,
                        type: 'button',
                    });
                }
            });
        }

        if (devStatus !== 'same' || btnChildren.length > 0) {
            devicesNode.children?.push({
                id: impDev.id,
                name: impDev.name,
                icon: impDev.icon,
                details: devStatus === 'same' ? t('configTransfer.status.changes', { count: btnChildren.length }) : t('configTransfer.status.' + devStatus),
                textClass: devStatus === 'new' ? 'text-green-400' : (devStatus === 'changed' ? 'text-orange-400' : ''),
                isOpen: false,
                selected: true,
                children: btnChildren,
                data: impDev,
                type: 'device',
            });
        }
    });

    if (devicesNode.children?.length) {
        devicesNode.selected = true;
        tree.push(devicesNode);
    }

    // ─── Automations ─────────────────────────────────────────────────────
    const automationsNode: TreeItem = {
        id: 'automations',
        name: 'Automations',
        icon: 'robot',
        children: [],
        isOpen: true,
        selected: false,
    };

    impAutos.forEach((impAuto: IRAutomation) => {
        const autoId = impAuto.id || '';

        // Duplicate automation ID within the import file
        if (autoId && duplicateAutoIds.has(autoId)) {
            automationsNode.children?.push({
                id: autoId,
                name: impAuto.name,
                icon: 'robot',
                details: t('configTransfer.status.duplicateId'),
                textClass: 'text-red-400',
                selected: false,
                isOpen: false,
                data: impAuto,
                type: 'automation',
            });
            return;
        }

        const currAuto = curAutos.find((a: IRAutomation) => a.id === autoId);
        let status = 'same';
        if (!currAuto) status = 'new';
        else if (!isDeepEqual(impAuto, currAuto)) status = 'changed';

        if (status !== 'same') {
            automationsNode.children?.push({
                id: autoId,
                name: impAuto.name,
                icon: 'robot',
                details: t('configTransfer.status.' + status),
                textClass: status === 'new' ? 'text-green-400' : 'text-orange-400',
                selected: true,
                isOpen: false,
                data: impAuto,
                type: 'automation',
            });
        }
    });

    if (automationsNode.children?.length) {
        automationsNode.selected = true;
        tree.push(automationsNode);
    }

    // Warn the user if any duplicate IDs were found
    const allDuplicateIds = [...duplicateDevIds, ...duplicateAutoIds];
    if (allDuplicateIds.length > 0) {
        commonStore.addFlashMessage(
            t('store.importDuplicate', { ids: allDuplicateIds.join(', ') }),
            'warning',
        );
    }

    importTree.value = tree;
};

const applyImport = () => {
    if (!importData.value || !rawConfig.value) return;
    
    // Deep copy current config as base
    const finalConfig: Config = JSON.parse(JSON.stringify(rawConfig.value));
    // Raw selection from the file (used for merge mode, backend handles dedupe)
    const incoming: Config = { devices: [], automations: [] };
    
    // Merge Devices
    const devicesNode = importTree.value.find((n) => n.id === 'devices');
    if (devicesNode && devicesNode.children) {
        devicesNode.children.forEach((devNode: TreeItem) => {
            if (!devNode.data || devNode.type !== 'device') return;
            // If device selected or has selected children
            const hasSelectedChildren = devNode.children && devNode.children.some((c: TreeItem) => c.selected);
            if (!devNode.selected && !hasSelectedChildren) return;

            // Collect the raw imported device (merge mode payload)
            const incomingDev: IRDevice = { ...(devNode.data as IRDevice), buttons: [] };
            devNode.children?.forEach((btnNode: TreeItem) => {
                if (btnNode.selected && btnNode.data && btnNode.type === 'button') {
                    incomingDev.buttons.push({ ...(btnNode.data as IRButton) });
                }
            });
            incoming.devices.push(incomingDev);

            let targetDev = finalConfig.devices.find((d: IRDevice) => d.id === (devNode.data as IRDevice).id);
            
            if (devNode.selected || devNode.indeterminate) {
                if (!targetDev) {
                    // New device: add shell, buttons added below
                    targetDev = { ...devNode.data as IRDevice, buttons: [] };
                    
                    // Ensure unique device name
                    let { name } = targetDev;
                    let counter = 1;
                    while (finalConfig.devices.some((d: IRDevice) => d.name.toLowerCase() === name.toLowerCase())) {
                        name = `${(devNode.data as IRDevice).name} (${counter})`;
                        counter += 1;
                    }
                    targetDev.name = name;
                    
                    finalConfig.devices.push(targetDev);
                } else {
                    // Update props
                    // Only update name if it changed, and ensure uniqueness if it did
                    if (targetDev.name !== (devNode.data as IRDevice).name) {
                        let name = (devNode.data as IRDevice).name;
                        let counter = 1;
                        while (finalConfig.devices.some((d: IRDevice) => d.id !== targetDev?.id && d.name.toLowerCase() === name.toLowerCase())) {
                            name = `${(devNode.data as IRDevice).name} (${counter})`;
                            counter += 1;
                        }
                        targetDev.name = name;
                    }
                    targetDev.icon = (devNode.data as IRDevice).icon;
                    targetDev.target_bridges = (devNode.data as IRDevice).target_bridges;
                }
            }

            if (targetDev && devNode.children) {
                devNode.children.forEach((btnNode: TreeItem) => {
                    if (btnNode.selected && btnNode.data && btnNode.type === 'button') {
                        const btnData = { ...(btnNode.data as IRButton) };
                        const existingBtnIndex = targetDev?.buttons.findIndex((b: IRButton) => b.id === btnData.id);
                        
                        // Ensure unique button name within device
                        let btnName = btnData.name;
                        let counter = 1;
                        while (targetDev?.buttons.some((b: IRButton) => b.name.toLowerCase() === btnName.toLowerCase() && b.id !== btnData.id)) {
                            btnName = `${(btnNode.data as IRButton).name} ${counter}`;
                            counter += 1;
                        }
                        btnData.name = btnName;

                        if (existingBtnIndex !== undefined && existingBtnIndex >= 0) {
                            (targetDev as IRDevice).buttons[existingBtnIndex] = btnData;
                        } else {
                            (targetDev as IRDevice).buttons.push(btnData);
                        }
                    }
                });
            }
        });
    }

    // Merge Automations
    const automationsNode = importTree.value.find((n) => n.id === 'automations');
    if (automationsNode && automationsNode.children) {
        automationsNode.children.forEach((autoNode: TreeItem) => {
            if (autoNode.selected && autoNode.data && autoNode.type === 'automation') {
                incoming.automations.push({ ...(autoNode.data as IRAutomation) });
                const autoData = { ...(autoNode.data as IRAutomation) };
                const existingIndex = finalConfig.automations.findIndex((a: IRAutomation) => a.id === autoData.id);
                
                // Ensure unique automation name
                let { name } = autoData;
                let counter = 1;
                while (finalConfig.automations.some((a: IRAutomation) => a.name.toLowerCase() === name.toLowerCase() && a.id !== autoData.id)) {
                    name = `${(autoNode.data as IRAutomation).name} (${counter})`;
                    counter += 1;
                }
                autoData.name = name;

                if (existingIndex >= 0) {
                    finalConfig.automations[existingIndex] = autoData;
                } else {
                    finalConfig.automations.push(autoData);
                }
            }
        });
    }

    // Send to backend ('replace' sends the merged result, 'merge' sends only the selection)
    const payload = importMode.value === 'merge' ? incoming : finalConfig;
    const blob = new Blob([JSON.stringify(payload, null, 2)], { type: 'application/json' });
    const formData = new FormData();
    formData.append('file', blob, 'import_config.json');
    formData.append('mode', importMode.value);

    api('config/import', {
        method: 'POST',
        body: formData,
    }).then(() => {
        commonStore.addFlashMessage(t('store.importSuccess'), 'success');
        emit('close');
    }).catch(() => {
        // Error handled by api wrapper (flash message)
    });
};


const formatBytes = (bytes: number): string => {
    if (!bytes) return '0 B';
    const units = ['B', 'KB', 'MB'];
    let value = bytes;
    let unit = 0;
    while (value >= 1024 && unit < units.length - 1) {
        value /= 1024;
        unit += 1;
    }
    return `${value.toFixed(unit === 0 ? 0 : 1)} ${units[unit]}`;
};

const formatDate = (iso: string): string => {
    const date = new Date(iso);
    return Number.isNaN(date.getTime()) ? iso : date.toLocaleString();
};

const fetchBackups = async () => {
    loadingBackups.value = true;
    try {
        const res = await api<{ backups: BackupMeta[] }>('backup/list');
        backups.value = res?.backups || [];
    } catch {
        // Error handled by api wrapper
    } finally {
        loadingBackups.value = false;
    }
};

const createBackup = async () => {
    creatingBackup.value = true;
    try {
        await api('backup/create', { method: 'POST' });
        commonStore.addFlashMessage(t('backup.created'), 'success');
        await fetchBackups();
    } catch {
        // Error handled by api wrapper
    } finally {
        creatingBackup.value = false;
    }
};

const downloadBackup = (backup: BackupMeta) => {
    const basePath = window.location.pathname.replace(/\/$/, '');
    const link = document.createElement('a');
    link.href = `${basePath}/api/backup/download/${backup.id}`;
    link.download = backup.filename;
    document.body.appendChild(link);
    link.click();
    link.remove();
};

const restoreBackup = async (backup: BackupMeta, event: MouseEvent) => {
    const isReplace = restoreMode.value === 'replace';
    const message = isReplace
        ? t('backup.restoreReplaceConfirm', { name: backup.filename })
        : t('backup.restoreMergeConfirm', { name: backup.filename });
    const confirmed = await commonStore.askConfirm(
        t('backup.restoreTitle'),
        message,
        isReplace ? 'danger' : 'warning',
        t('backup.restoreBtn'),
        event,
    );
    if (!confirmed) return;

    restoringId.value = backup.id;
    try {
        await api(`backup/restore/${backup.id}?mode=${restoreMode.value}`, { method: 'POST' });
        commonStore.addFlashMessage(t('backup.restored'), 'success');
        await fetchConfig();
        await fetchBackups();
    } catch {
        // Error handled by api wrapper
    } finally {
        restoringId.value = null;
    }
};

const deleteBackup = async (backup: BackupMeta, event: MouseEvent) => {
    const confirmed = await commonStore.askConfirm(
        t('backup.deleteTitle'),
        t('backup.deleteConfirm', { name: backup.filename }),
        'danger',
        t('backup.deleteBtn'),
        event,
    );
    if (!confirmed) return;
    try {
        await api(`backup/${backup.id}`, { method: 'DELETE' });
        commonStore.addFlashMessage(t('backup.deleted'), 'success');
        await fetchBackups();
    } catch {
        // Error handled by api wrapper
    }
};

watch(mode, (newMode) => {
    if (newMode === 'backups') {
        fetchBackups();
    }
});

watch(() => props.show, (newVal) => {
    if (newVal) {
        if (mode.value === 'backups') {
            fetchBackups();
        } else {
            fetchConfig();
        }
    }
});

</script>

<template>
  <div
    v-if="show"
    class="fixed inset-0 !m-0 bg-gray-900/60 z-50 flex justify-center items-center p-4 backdrop-blur-sm"
  >
    <div
      class="bg-gray-900 border border-gray-700 rounded-lg shadow-2xl p-8 w-full max-w-4xl flex flex-col animate-in fade-in scale-95 duration-200"
      style="height: 90vh; animation: slideInUp 0.3s ease-out;"
      data-tour-id="config-transfer-modal"
    >
      <header class="flex justify-between items-center mb-4">
        <div class="flex items-center gap-3">
          <h2 class="text-2xl font-bold">
            {{ t('configTransfer.title') }}
          </h2>
          <button
            class="text-gray-500 hover:text-gray-300 transition-colors"
            :title="t('app.startTourDefault')"
            @click="startConfigTransferTour"
          >
            <i class="mdi mdi-help-circle-outline text-xl" />
          </button>
        </div>
        <div
          class="flex gap-2"
          data-tour-id="config-mode-switch"
        >
          <button
            :class="['btn btn-sm', { 'btn-primary': mode === 'export', 'btn-secondary': mode !== 'export' }]"
            data-tour-id="config-mode-export"
            @click="mode = 'export'"
          >
            {{ t('configTransfer.export') }}
          </button>
          <button
            :class="['btn btn-sm', { 'btn-primary': mode === 'import', 'btn-secondary': mode !== 'import' }]"
            data-tour-id="config-mode-import"
            @click="mode = 'import'"
          >
            {{ t('configTransfer.import') }}
          </button>
          <button
            :class="['btn btn-sm', { 'btn-primary': mode === 'backups', 'btn-secondary': mode !== 'backups' }]"
            data-tour-id="config-mode-backups"
            @click="mode = 'backups'"
          >
            {{ t('backup.tab') }}
          </button>
        </div>
      </header>
            
      <main class="flex-grow overflow-y-auto">
        <div v-if="mode === 'export'">
          <h3 class="text-lg font-semibold mb-2">
            {{ t('configTransfer.exportTitle') }}
          </h3>
          <p class="text-sm text-gray-400 mb-4">
            {{ t('configTransfer.exportDesc') }}
          </p>
          <div
            class="bg-gray-800 border border-gray-700 p-4 rounded-lg"
            data-tour-id="config-tree-view"
          >
            <TreeView
              :items="configTree"
              @update:model-value="configTree = [...configTree]"
            />
          </div>
        </div>

        <div v-if="mode === 'import'">
          <h3 class="text-lg font-semibold mb-2">
            {{ t('configTransfer.importTitle') }}
          </h3>
          <p class="text-sm text-gray-400 mb-4">
            {{ t('configTransfer.importDesc') }}
          </p>

          <div class="flex items-center gap-3 mb-2">
            <span class="text-sm font-medium text-gray-400">{{ t('configTransfer.importMode') }}</span>
            <div class="flex bg-gray-900 rounded p-1 border border-gray-600">
              <button
                class="px-3 py-1 rounded text-xs font-bold transition-colors min-w-[80px]"
                :class="importMode === 'merge' ? 'bg-blue-600 text-white' : 'text-gray-400 hover:text-gray-200'"
                data-tour-id="config-mode-merge"
                @click="importMode = 'merge'"
              >
                {{ t('configTransfer.modeMerge') }}
              </button>
              <button
                class="px-3 py-1 rounded text-xs font-bold transition-colors min-w-[80px]"
                :class="importMode === 'replace' ? 'bg-red-600 text-white' : 'text-gray-400 hover:text-gray-200'"
                data-tour-id="config-mode-replace"
                @click="importMode = 'replace'"
              >
                {{ t('configTransfer.modeReplace') }}
              </button>
            </div>
          </div>
          <p
            v-if="importMode === 'replace'"
            class="text-xs text-yellow-500 mb-4"
          >
            <i class="mdi mdi-alert-outline" /> {{ t('configTransfer.modeReplaceWarning') }}
          </p>
          <p
            v-else
            class="text-xs text-gray-400 mb-4"
          >
            {{ t('configTransfer.modeMergeDesc') }}
          </p>

          <input
            type="file"
            accept="application/json"
            class="mb-4"
            data-tour-id="config-file-input"
            @change="handleFileSelect"
          >

          <div
            v-if="importTree.length > 0"
            class="bg-gray-800 border border-gray-700 p-4 rounded-lg"
            data-tour-id="config-import-tree"
          >
            <h4 class="text-md font-semibold mb-2">
              {{ t('configTransfer.changesFound') }}
            </h4>
            <p class="text-xs text-gray-400 mb-2">
              {{ t('configTransfer.mergeDesc') }}
            </p>
            <TreeView
              :items="importTree"
              @update:model-value="importTree = [...importTree]"
            />
          </div>
          <div
            v-else-if="importData && importTree.length === 0"
            class="text-center text-gray-500 py-4"
          >
            {{ t('configTransfer.noChanges') }}
          </div>
        </div>

        <div v-if="mode === 'backups'">
          <div class="flex justify-between items-start mb-4 gap-4">
            <div>
              <h3 class="text-lg font-semibold mb-1">
                {{ t('backup.title') }}
              </h3>
              <p class="text-sm text-gray-400">
                {{ t('backup.description') }}
              </p>
            </div>
            <button
              class="btn btn-primary whitespace-nowrap"
              :disabled="creatingBackup"
              data-tour-id="backup-create-btn"
              @click="createBackup"
            >
              <i
                class="mdi"
                :class="creatingBackup ? 'mdi-loading animate-spin' : 'mdi-content-save'"
              /> {{ creatingBackup ? t('backup.creating') : t('backup.createBtn') }}
            </button>
          </div>

          <div class="flex items-center gap-3 mb-4">
            <span class="text-sm font-medium text-gray-400">{{ t('backup.restoreMode') }}</span>
            <div class="flex bg-gray-900 rounded p-1 border border-gray-600">
              <button
                class="px-3 py-1 rounded text-xs font-bold transition-colors min-w-[80px]"
                :class="restoreMode === 'merge' ? 'bg-blue-600 text-white' : 'text-gray-400 hover:text-gray-200'"
                @click="restoreMode = 'merge'"
              >
                {{ t('configTransfer.modeMerge') }}
              </button>
              <button
                class="px-3 py-1 rounded text-xs font-bold transition-colors min-w-[80px]"
                :class="restoreMode === 'replace' ? 'bg-red-600 text-white' : 'text-gray-400 hover:text-gray-200'"
                @click="restoreMode = 'replace'"
              >
                {{ t('configTransfer.modeReplace') }}
              </button>
            </div>
          </div>

          <div
            v-if="loadingBackups"
            class="text-center text-gray-500 py-6"
          >
            {{ t('backup.loading') }}
          </div>
          <div
            v-else-if="backups.length === 0"
            class="text-center text-gray-500 py-6"
          >
            {{ t('backup.empty') }}
          </div>
          <div
            v-else
            class="bg-gray-800 border border-gray-700 rounded-lg overflow-hidden"
          >
            <table class="w-full text-sm">
              <thead class="bg-gray-900 text-gray-400 text-xs uppercase">
                <tr>
                  <th class="text-left px-3 py-2">
                    {{ t('backup.colCreated') }}
                  </th>
                  <th class="text-left px-3 py-2">
                    {{ t('backup.colSize') }}
                  </th>
                  <th class="text-left px-3 py-2">
                    {{ t('backup.colDevices') }}
                  </th>
                  <th class="text-left px-3 py-2">
                    {{ t('backup.colAutomations') }}
                  </th>
                  <th class="text-right px-3 py-2">
                    {{ t('backup.colActions') }}
                  </th>
                </tr>
              </thead>
              <tbody>
                <tr
                  v-for="b in backups"
                  :key="b.id"
                  class="border-t border-gray-700"
                >
                  <td class="px-3 py-2 whitespace-nowrap">
                    {{ formatDate(b.created_at) }}
                  </td>
                  <td class="px-3 py-2">
                    {{ formatBytes(b.size) }}
                  </td>
                  <td class="px-3 py-2">
                    {{ b.device_count }}
                  </td>
                  <td class="px-3 py-2">
                    {{ b.automation_count }}
                  </td>
                  <td class="px-3 py-2">
                    <div class="flex justify-end gap-2">
                      <button
                        class="btn btn-secondary btn-sm"
                        :disabled="restoringId === b.id"
                        @click="restoreBackup(b, $event)"
                      >
                        <i class="mdi mdi-backup-restore" /> {{ t('backup.restoreBtn') }}
                      </button>
                      <button
                        class="btn btn-secondary btn-sm"
                        @click="downloadBackup(b)"
                      >
                        <i class="mdi mdi-download" /> {{ t('backup.downloadBtn') }}
                      </button>
                      <button
                        class="btn btn-danger btn-sm"
                        @click="deleteBackup(b, $event)"
                      >
                        <i class="mdi mdi-delete" /> {{ t('backup.deleteBtn') }}
                      </button>
                    </div>
                  </td>
                </tr>
              </tbody>
            </table>
          </div>
        </div>
      </main>

      <footer class="flex justify-end gap-4 mt-8">
        <button
          class="btn btn-secondary"
          @click="$emit('close')"
        >
          {{ t('configTransfer.close') }}
        </button>
        <button
          v-if="mode === 'export'"
          class="btn btn-primary"
          data-tour-id="config-action-button"
          @click="downloadConfig"
        >
          <i class="mdi mdi-download" /> {{ t('configTransfer.downloadBtn') }}
        </button>
        <button
          v-if="mode === 'import'"
          class="btn btn-primary"
          :disabled="!importData"
          data-tour-id="config-action-button"
          @click="applyImport"
        >
          <i class="mdi mdi-upload" /> {{ t('configTransfer.applyBtn') }}
        </button>
      </footer>
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
