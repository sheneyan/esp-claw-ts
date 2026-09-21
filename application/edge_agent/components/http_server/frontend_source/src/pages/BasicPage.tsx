import { createEffect, createSignal, Index, onMount, Show, type Component } from 'solid-js';
import { currentLocale, t, tf } from '../i18n';
import {
  connectWifiProfile,
  fetchWifiProfiles,
  saveWifiProfiles,
  type AppConfig,
  type WifiProfileInput,
} from '../api/client';
import { createConfigTab } from '../state/configTab';
import { appStatus } from '../state/config';
import { TabShell } from '../components/layout/TabShell';
import { PageHeader } from '../components/ui/PageHeader';
import { CollapsibleConfigBlock, StaticConfigBlock } from '../components/ui/ConfigBlocks';
import { TextInput, SelectInput } from '../components/ui/FormField';
import { SavePanel } from '../components/ui/SavePanel';
import { Banner } from '../components/ui/Banner';
import { RestartConfirmModal } from '../components/system/RestartConfirmModal';
import { pushToast } from '../state/toast';

type BasicForm = {
  ap_ssid: string;
  ap_password: string;
  ap_behavior: string;
  time_timezone: string;
};

type WifiProfileDraft = {
  ssid: string;
  password: string;
  passwordSet: boolean;
  active: boolean;
  clearPassword: boolean;
};

export const BasicPage: Component<{ onRestartRequest: () => void }> = (props) => {
  const tab = createConfigTab<BasicForm>({
    tab: 'basic',
    groups: ['wifi', 'time'],
    toForm: (config: Partial<AppConfig>) => ({
      ap_ssid: config.ap_ssid ?? '',
      ap_password: config.ap_password ?? '',
      ap_behavior: config.ap_behavior ?? 'keep',
      time_timezone: config.time_timezone ?? '',
    }),
    fromForm: (form) => ({
      ap_ssid: form.ap_ssid.trim(),
      ap_password: form.ap_password,
      ap_behavior: form.ap_behavior,
      time_timezone: form.time_timezone.trim(),
    }),
  });
  const [validationError, setValidationError] = createSignal<string | null>(null);
  const [confirmOpen, setConfirmOpen] = createSignal(false);
  const [profiles, setProfiles] = createSignal<WifiProfileDraft[]>([]);
  const [profilesDirty, setProfilesDirty] = createSignal(false);
  const [profilesBusy, setProfilesBusy] = createSignal(false);
  const [profilesError, setProfilesError] = createSignal<string | null>(null);

  const loadProfiles = async () => {
    setProfilesBusy(true);
    setProfilesError(null);
    try {
      const loaded = await fetchWifiProfiles();
      setProfiles(
        loaded
          .filter((profile) => profile.configured)
          .map((profile) => ({
            ssid: profile.ssid,
            password: '',
            passwordSet: profile.password_set,
            active: profile.active,
            clearPassword: false,
          })),
      );
      setProfilesDirty(false);
    } catch (error) {
      setProfilesError(
        error instanceof Error ? error.message : (t('wifiProfilesLoadError') as string),
      );
    } finally {
      setProfilesBusy(false);
    }
  };

  onMount(() => void loadProfiles());

  createEffect(() => {
    void tab.form.ap_ssid;
    void tab.form.ap_password;
    setValidationError(null);
  });

  const handleSave = async () => {
    const apPassword = tab.form.ap_password;

    if (apPassword.length > 0 && apPassword.length < 8) {
      const message = t('apValidationPasswordLength') as string;
      setValidationError(message);
      pushToast(message, 'error', 5000);
      return;
    }

    await tab.save();
    setConfirmOpen(true);
  };

  const updateProfile = (index: number, patch: Partial<WifiProfileDraft>) => {
    setProfiles((items) =>
      items.map((item, itemIndex) => (itemIndex === index ? { ...item, ...patch } : item)),
    );
    setProfilesDirty(true);
    setProfilesError(null);
  };

  const moveProfile = (index: number, offset: -1 | 1) => {
    const nextIndex = index + offset;
    if (nextIndex < 0 || nextIndex >= profiles().length) return;
    setProfiles((items) => {
      const next = [...items];
      const current = next[index]!;
      next[index] = next[nextIndex]!;
      next[nextIndex] = current;
      return next;
    });
    setProfilesDirty(true);
  };

  const saveProfiles = async () => {
    const rows = profiles();
    const normalized = rows.map((profile) => profile.ssid.trim());
    if (normalized.some((ssid) => !ssid)) {
      setProfilesError(t('wifiValidationSsidRequired') as string);
      return;
    }
    if (new Set(normalized).size !== normalized.length) {
      setProfilesError(t('wifiProfilesDuplicate') as string);
      return;
    }
    if (rows.some((profile) => profile.password.length > 0 && profile.password.length < 8)) {
      setProfilesError(t('wifiValidationPasswordLength') as string);
      return;
    }
    const payload: WifiProfileInput[] = rows.map((profile, index) => ({
      ssid: normalized[index]!,
      ...(profile.clearPassword
        ? { clear_password: true }
        : profile.password
          ? { password: profile.password }
          : {}),
    }));
    setProfilesBusy(true);
    setProfilesError(null);
    try {
      await saveWifiProfiles(payload);
      pushToast(t('wifiProfilesSaved') as string, 'success');
      await loadProfiles();
    } catch (error) {
      setProfilesError(
        error instanceof Error ? error.message : (t('wifiProfilesSaveError') as string),
      );
      setProfilesBusy(false);
    }
  };

  const connectProfile = async (index: number) => {
    if (!window.confirm(t('wifiProfilesConnectConfirm') as string)) return;
    setProfilesBusy(true);
    try {
      await connectWifiProfile(index);
      pushToast(t('wifiProfilesConnectAccepted') as string, 'success');
    } catch (error) {
      setProfilesError(
        error instanceof Error ? error.message : (t('wifiProfilesConnectError') as string),
      );
    } finally {
      setProfilesBusy(false);
    }
  };

  const currentApSsid = () => appStatus()?.ap_ssid ?? '';

  const apNameHint = () => {
    const ssid = currentApSsid();
    return ssid ? tf('apNameHint', { ssid }) : '';
  };

  const timezoneHint = () =>
    currentLocale() === 'zh-cn' ? (
      <>
        仅接受 POSIX TZ 字符串，符号与日常 UTC 表示相反。北京时间（UTC+8）应写作 "CST-8"
        ，纽约（UTC-5）写作 "EST5"。可在
        <a
          href="https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv"
          target="_blank"
          rel="noopener noreferrer"
          class="underline underline-offset-2 hover:text-[var(--color-text-primary)]"
        >
          此表格
        </a>
        查阅 IANA 时区与 POSIX 表达转换关系。
      </>
    ) : (
      (t('timezoneHelp') as string)
    );

  return (
    <TabShell>
      <PageHeader title={t('navBasic') as string} description={t('restartHint') as string} />
      <Show when={validationError() ?? tab.error()}>
        <div class="px-5 pt-4">
          <Banner kind="error" message={validationError() ?? tab.error() ?? undefined} />
        </div>
      </Show>
      <div class="divide-y divide-[var(--color-border-subtle)] mt-2">
        <StaticConfigBlock title={t('sectionWifi') as string}>
          <div class="space-y-3 pt-2">
            <Show when={profilesError()}>
              <Banner kind="error" message={profilesError() ?? undefined} />
            </Show>
            <Index each={profiles()}>
              {(profile, index) => (
                <div class="rounded-[var(--radius-sm)] border border-[var(--color-border-subtle)] p-3">
                  <div class="mb-2 flex items-center justify-between gap-2">
                    <span class="text-sm font-medium">
                      {t('wifiProfilePriority') as string} {index + 1}
                      <Show when={profile().active}> · {t('wifiProfileActive') as string}</Show>
                    </span>
                    <div class="flex flex-wrap gap-1.5">
                      <button
                        type="button"
                        disabled={index === 0 || profilesBusy()}
                        onClick={() => moveProfile(index, -1)}
                        class="rounded border border-[var(--color-border-subtle)] px-2 py-1 text-xs disabled:opacity-40"
                      >
                        {t('wifiProfileUp') as string}
                      </button>
                      <button
                        type="button"
                        disabled={index === profiles().length - 1 || profilesBusy()}
                        onClick={() => moveProfile(index, 1)}
                        class="rounded border border-[var(--color-border-subtle)] px-2 py-1 text-xs disabled:opacity-40"
                      >
                        {t('wifiProfileDown') as string}
                      </button>
                      <button
                        type="button"
                        disabled={profilesDirty() || profilesBusy()}
                        onClick={() => void connectProfile(index)}
                        class="rounded border border-[var(--color-border-subtle)] px-2 py-1 text-xs disabled:opacity-40"
                      >
                        {t('wifiProfileConnect') as string}
                      </button>
                      <button
                        type="button"
                        disabled={profilesBusy()}
                        onClick={() => {
                          setProfiles((items) =>
                            items.filter((_, itemIndex) => itemIndex !== index),
                          );
                          setProfilesDirty(true);
                        }}
                        class="rounded border border-[var(--color-danger)]/40 px-2 py-1 text-xs text-[var(--color-danger)] disabled:opacity-40"
                      >
                        {t('wifiProfileRemove') as string}
                      </button>
                    </div>
                  </div>
                  <div class="grid gap-3 sm:grid-cols-2">
                    <TextInput
                      label={t('wifiSsid')}
                      value={profile().ssid}
                      onInput={(event) =>
                        updateProfile(index, { ssid: event.currentTarget.value })
                      }
                    />
                    <TextInput
                      type="password"
                      label={t('wifiPassword')}
                      placeholder={
                        profile().passwordSet ? (t('wifiProfilePasswordKeep') as string) : ''
                      }
                      value={profile().password}
                      disabled={profile().clearPassword}
                      onInput={(event) =>
                        updateProfile(index, {
                          password: event.currentTarget.value,
                          clearPassword: false,
                        })
                      }
                    />
                  </div>
                  <label class="mt-2 flex items-center gap-2 text-xs text-[var(--color-text-secondary)]">
                    <input
                      type="checkbox"
                      checked={profile().clearPassword}
                      onChange={(event) =>
                        updateProfile(index, {
                          clearPassword: event.currentTarget.checked,
                          password: '',
                        })
                      }
                    />
                    {t('wifiProfileOpenNetwork') as string}
                  </label>
                </div>
              )}
            </Index>
            <Show when={profiles().length === 0 && !profilesBusy()}>
              <p class="text-sm text-[var(--color-text-secondary)]">
                {t('wifiProfilesEmpty') as string}
              </p>
            </Show>
            <div class="flex flex-wrap gap-2">
              <button
                type="button"
                disabled={profiles().length >= 5 || profilesBusy()}
                onClick={() => {
                  setProfiles((items) => [
                    ...items,
                    {
                      ssid: '',
                      password: '',
                      passwordSet: false,
                      active: false,
                      clearPassword: false,
                    },
                  ]);
                  setProfilesDirty(true);
                }}
                class="rounded border border-[var(--color-border-subtle)] px-3 py-2 text-sm disabled:opacity-40"
              >
                {t('wifiProfileAdd') as string}
              </button>
              <button
                type="button"
                disabled={!profilesDirty() || profilesBusy()}
                onClick={() => void saveProfiles()}
                class="rounded bg-[var(--color-accent)] px-3 py-2 text-sm text-white disabled:opacity-40"
              >
                {t('wifiProfilesSave') as string}
              </button>
              <button
                type="button"
                disabled={!profilesDirty() || profilesBusy()}
                onClick={() => void loadProfiles()}
                class="rounded border border-[var(--color-border-subtle)] px-3 py-2 text-sm disabled:opacity-40"
              >
                {t('discardBtn') as string}
              </button>
            </div>
            <p class="text-xs text-[var(--color-text-secondary)]">
              {t('wifiProfilesHint') as string}
            </p>
          </div>
          <div class="grid gap-3 sm:grid-cols-2 pt-5 mt-5 border-t border-[var(--color-border-subtle)]">
            <TextInput
              label={t('apName')}
              autocomplete="off"
              hint={apNameHint()}
              value={tab.form.ap_ssid}
              onInput={(event) => tab.setForm('ap_ssid', event.currentTarget.value)}
            />
            <TextInput
              type="password"
              label={t('apPassword')}
              autocomplete="new-password"
              hint={t('apPasswordHint') as string}
              value={tab.form.ap_password}
              onInput={(event) => tab.setForm('ap_password', event.currentTarget.value)}
            />
            <SelectInput
              label={t('apBehavior')}
              value={tab.form.ap_behavior}
              onChange={(event) => tab.setForm('ap_behavior', event.currentTarget.value)}
            >
              <option value="keep">{t('apBehaviorKeep') as string}</option>
              <option value="close_on_sta">{t('apBehaviorCloseOnSta') as string}</option>
            </SelectInput>
          </div>
        </StaticConfigBlock>
        <CollapsibleConfigBlock title={t('sectionAdvanced') as string} defaultOpen={false}>
          <div class="pt-2">
            <TextInput
              full
              label={t('timezone')}
              placeholder={t('timezonePlaceholder') as string}
              hint={timezoneHint()}
              value={tab.form.time_timezone}
              onInput={(event) => tab.setForm('time_timezone', event.currentTarget.value)}
            />
          </div>
        </CollapsibleConfigBlock>
      </div>
      <SavePanel
        dirty={tab.dirty()}
        saving={tab.saving()}
        onSave={() => handleSave().catch(() => undefined)}
        onDiscard={tab.discard}
        note={t('restartHint') as string}
      />
      <RestartConfirmModal
        open={confirmOpen()}
        onClose={() => setConfirmOpen(false)}
        onConfirm={() => {
          setConfirmOpen(false);
          props.onRestartRequest();
        }}
        subtitle={t('restartHint') as string}
      />
    </TabShell>
  );
};
