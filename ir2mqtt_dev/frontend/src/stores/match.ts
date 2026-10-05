import { defineStore } from 'pinia';
import { ref } from 'vue';
import { api } from '../services/api';

export interface MatchCategory {
    id: number;
    name: string;
    count: number;
}

export interface MatchBrand {
    id: string;
    name: string;
    count: number;
}

export interface MatchCandidate {
    remote_id: string;
    remote_key: string;
    frequency: number;
    is_ac: boolean;
    rank: number;
    country: string | null;
    label: string;
}

export interface MatchCountries {
    countries: string[];
    recommended: string | null;
}

export interface MatchAcState {
    power: number;
    mode: number;
    temperature: number;
    wind_speed: number;
    lr_wind_mode: number;
    ud_wind_mode: number;
}

export interface MatchTestResult {
    sent: boolean;
    button_name: string;
    is_ac: boolean;
    state?: MatchAcState | null;
    function_id?: number | null;
    targets: string[];
}

export const useMatchStore = defineStore('match', () => {
    const available = ref<boolean | null>(null);
    const categories = ref<MatchCategory[]>([]);
    const brands = ref<MatchBrand[]>([]);
    const candidates = ref<MatchCandidate[]>([]);

    const fetchAvailable = async () => {
        const data = await api<{ available: boolean }>('match/available');
        available.value = data?.available ?? false;
        return available.value;
    };

    const fetchCategories = async () => {
        const data = await api<MatchCategory[]>('match/categories');
        categories.value = data ?? [];
        return categories.value;
    };

    const fetchBrands = async (categoryId: number) => {
        const data = await api<MatchBrand[]>(`match/brands?category_id=${categoryId}`);
        brands.value = data ?? [];
        return brands.value;
    };

    const fetchCandidates = async (categoryId: number, brandId: string, country?: string | null) => {
        const params = new URLSearchParams({
            category_id: String(categoryId),
            brand_id: brandId,
        });
        if (country) params.set('country', country);
        const data = await api<MatchCandidate[]>(`match/candidates?${params.toString()}`);
        candidates.value = data ?? [];
        return candidates.value;
    };

    const fetchCountries = async (categoryId: number, brandId: string) => {
        const params = new URLSearchParams({
            category_id: String(categoryId),
            brand_id: brandId,
        });
        return api<MatchCountries>(`match/countries?${params.toString()}`);
    };

    const testRemote = (
        categoryId: number,
        brandId: string,
        remoteId: string,
        targets: string[],
        state?: Partial<MatchAcState> & { function_id?: number },
    ) => api<MatchTestResult>('match/test', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
            category_id: categoryId,
            brand_id: brandId,
            remote_id: remoteId,
            target: targets.length > 0 ? targets : null,
            ...(state ?? {}),
        }),
    });

    const saveRemote = (
        categoryId: number,
        brandId: string,
        remoteId: string,
        name: string,
        targetBridges: string[],
    ) => api('match/save', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
            category_id: categoryId,
            brand_id: brandId,
            remote_id: remoteId,
            name,
            target_bridges: targetBridges,
        }),
    });

    return {
        available,
        categories,
        brands,
        candidates,
        fetchAvailable,
        fetchCategories,
        fetchBrands,
        fetchCandidates,
        fetchCountries,
        testRemote,
        saveRemote,
    };
});
