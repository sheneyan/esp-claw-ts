import { createMemo, createSignal, For, onCleanup, onMount, Show, type Component } from 'solid-js';
import {
  clearTailscaleExitNode,
  fetchTailscaleExitNodes,
  fetchTailscaleStatus,
  setTailscaleExitNode,
  type AppConfig,
  type TailscaleExitNode,
  type TailscaleOperation,
  type TailscaleStatus,
} from '../api/client';
import { TabShell } from '../components/layout/TabShell';
import { Banner } from '../components/ui/Banner';
import { Button } from '../components/ui/Button';
import { StaticConfigBlock } from '../components/ui/ConfigBlocks';
import { SelectInput, TextInput } from '../components/ui/FormField';
import { PageHeader } from '../components/ui/PageHeader';
import { SavePanel } from '../components/ui/SavePanel';
import { Switch } from '../components/ui/Switch';
import { t } from '../i18n';
import { appConfig, isGroupLoaded, patchConfigLocal } from '../state/config';
import { createConfigTab } from '../state/configTab';
import { pushToast } from '../state/toast';

const STATUS_POLL_MS = 5000;

type TailscaleForm = {
  tailscale_enabled: boolean;
  tailscale_auth_key: string;
  tailscale_hostname: string;
  tailscale_login_server: string;
  tailscale_exit_node: string;
  tailscale_max_peers: string;
};

function parseBool(value: string | undefined): boolean {
  return value === 'true' || value === '1';
}

const InfoRow: Component<{ label: string; value?: string; mono?: boolean }> = (props) => (
  <div class="flex items-center justify-between gap-3 rounded-[var(--radius-sm)] border border-transparent bg-white/[0.02] px-3 py-2 hover:border-[var(--color-border-subtle)]">
    <span class="text-[0.78rem] font-semibold uppercase tracking-wider text-[var(--color-text-muted)]">
      {props.label}
    </span>
    <span
      class={[
        'break-all text-right text-[0.88rem] text-[var(--color-text-primary)]',
        props.mono ? 'font-mono' : '',
      ].join(' ')}
    >
      {props.value || (t('sysInfoNone') as string)}
    </span>
  </div>
);

function exitNodeLabel(node: TailscaleExitNode): string {
  const name = node.hostname || node.ip;
  const path = node.direct
    ? (t('tailscaleExitDirect') as string)
    : node.derp_region > 0
      ? `${t('tailscaleExitDerp')} ${node.derp_region}`
      : (t('tailscaleExitRelay') as string);
  return `${name} (${node.ip}) · ${node.online ? t('tailscaleOnline') : t('tailscaleOffline')} · ${path}`;
}

export const TailscalePage: Component = () => {
  const tab = createConfigTab<TailscaleForm>({
    tab: 'tailscale',
    groups: ['tailscale'],
    toForm: (config: Partial<AppConfig>) => ({
      tailscale_enabled: parseBool(config.tailscale_enabled),
      // The auth key is write-only and must never be copied into the form.
      tailscale_auth_key: '',
      tailscale_hostname: config.tailscale_hostname ?? '',
      tailscale_login_server: config.tailscale_login_server ?? '',
      tailscale_exit_node: config.tailscale_exit_node ?? '',
      tailscale_max_peers: config.tailscale_max_peers ?? '16',
    }),
    fromForm: (form) => ({
      tailscale_enabled: form.tailscale_enabled ? 'true' : 'false',
      tailscale_auth_key: form.tailscale_auth_key.trim(),
      tailscale_hostname: form.tailscale_hostname.trim(),
      tailscale_login_server: form.tailscale_login_server.trim(),
      tailscale_max_peers: form.tailscale_max_peers.trim(),
    }),
  });
  const [status, setStatus] = createSignal<TailscaleStatus | null>(null);
  const [exitNodes, setExitNodes] = createSignal<TailscaleExitNode[]>([]);
  const [exitNodesLoaded, setExitNodesLoaded] = createSignal(false);
  const [runtimeError, setRuntimeError] = createSignal<string | null>(null);
  const [exitNodesError, setExitNodesError] = createSignal<string | null>(null);
  const [mutationError, setMutationError] = createSignal<string | null>(null);
  const [mutatingExitNode, setMutatingExitNode] = createSignal(false);
  const [refreshing, setRefreshing] = createSignal(false);
  const [validationError, setValidationError] = createSignal<string | null>(null);
  let pollTimer: ReturnType<typeof setInterval> | undefined;
  let requestController: AbortController | undefined;

  const refreshRuntime = async (interactive = false) => {
    // Let an in-flight request finish instead of having the background timer
    // repeatedly cancel it on a slow connection. A manual refresh may replace
    // an older background request.
    if (requestController && !interactive) return;
    if (interactive) requestController?.abort();
    const controller = new AbortController();
    requestController = controller;
    if (interactive) setRefreshing(true);
    try {
      const [statusResult, exitNodesResult] = await Promise.allSettled([
        fetchTailscaleStatus(controller.signal),
        fetchTailscaleExitNodes(controller.signal),
      ]);
      if (controller.signal.aborted) return;
      const errors: string[] = [];
      if (statusResult.status === 'fulfilled') {
        setStatus(statusResult.value);
      } else if ((statusResult.reason as Error).name !== 'AbortError') {
        errors.push(
          (statusResult.reason as Error).message || (t('tailscaleStatusUnavailable') as string),
        );
      }
      if (exitNodesResult.status === 'fulfilled') {
        setExitNodes(exitNodesResult.value);
        setExitNodesLoaded(true);
        setExitNodesError(null);
      } else if ((exitNodesResult.reason as Error).name !== 'AbortError') {
        setExitNodesLoaded(false);
        setExitNodesError((exitNodesResult.reason as Error).message);
      }
      setRuntimeError(errors.length > 0 ? Array.from(new Set(errors)).join(' · ') : null);
    } finally {
      if (requestController === controller) {
        requestController = undefined;
        if (interactive) setRefreshing(false);
      }
    }
  };

  onMount(() => {
    void refreshRuntime();
    pollTimer = setInterval(() => void refreshRuntime(), STATUS_POLL_MS);
  });

  onCleanup(() => {
    if (pollTimer !== undefined) clearInterval(pollTimer);
    requestController?.abort();
  });

  const authKeyConfigured = () =>
    appConfig().tailscale_auth_key_set ?? status()?.auth_key_set ?? false;

  const selectedExitIsListed = createMemo(() =>
    exitNodes().some((node) => node.ip === tab.form.tailscale_exit_node),
  );

  const peersText = () => {
    const current = status();
    return current ? `${current.peer_online} / ${current.peer_count}` : '';
  };

  const mutationErrorMessage = (error: Error & { operation?: TailscaleOperation }) => {
    const operation = error.operation;
    if (!operation?.rollback_attempted) return error.message;
    return operation.rollback_recovered
      ? `${error.message} ${t('tailscaleRollbackRecovered')}`
      : `${error.message} ${t('tailscaleRollbackFailed')}`;
  };

  const handleExitNodeChange = async (nextNode: string) => {
    if (mutatingExitNode()) return;
    setMutatingExitNode(true);
    setMutationError(null);
    try {
      if (nextNode) {
        await setTailscaleExitNode(nextNode);
      } else {
        await clearTailscaleExitNode();
      }
    } catch (error) {
      setMutationError(mutationErrorMessage(error as Error & { operation?: TailscaleOperation }));
    } finally {
      await Promise.allSettled([refreshRuntime(true), tab.reload()]);
      setMutatingExitNode(false);
    }
  };

  const handleSave = async () => {
    if (!isGroupLoaded('tailscale')) return;

    const maxPeers = tab.form.tailscale_max_peers.trim();
    if (!/^[1-9]\d*$/.test(maxPeers) || Number(maxPeers) > 64) {
      const message = t('tailscaleMaxPeersValidation') as string;
      setValidationError(message);
      pushToast(message, 'error', 5000);
      return;
    }

    setValidationError(null);
    const submittedAuthKey = tab.form.tailscale_auth_key.trim().length > 0;
    await tab.save();
    if (submittedAuthKey) {
      patchConfigLocal({ tailscale_auth_key_set: true });
    }
    void refreshRuntime();
  };

  return (
    <TabShell>
      <PageHeader
        title={t('navTailscale') as string}
        description={t('tailscaleDescription') as string}
        actions={
          <Button
            size="sm"
            variant="secondary"
            disabled={refreshing()}
            onClick={() => void refreshRuntime(true)}
          >
            {refreshing() ? '…' : t('tailscaleRefresh')}
          </Button>
        }
      />
      <Show when={validationError() ?? tab.error()}>
        <div class="px-5 pt-4">
          <Banner kind="error" message={validationError() ?? tab.error() ?? undefined} />
        </div>
      </Show>
      <Show when={runtimeError()}>
        <div class="px-5 pt-4">
          <Banner kind="info" message={runtimeError() ?? undefined} />
        </div>
      </Show>
      <div class="mt-2 divide-y divide-[var(--color-border-subtle)]">
        <StaticConfigBlock title={t('tailscaleSectionStatus') as string}>
          <div class="grid gap-2 pt-2 sm:grid-cols-2">
            <InfoRow
              label={t('tailscaleConnection') as string}
              value={
                status()
                  ? status()!.connected
                    ? (t('tailscaleConnected') as string)
                    : (t('tailscaleDisconnected') as string)
                  : ''
              }
            />
            <InfoRow label={t('tailscalePath') as string} value={status()?.path} />
            <InfoRow label={t('tailscaleVpnIp') as string} value={status()?.vpn_ip} mono />
            <InfoRow label={t('tailscalePeers') as string} value={peersText()} />
            <InfoRow
              label={t('tailscaleSelectedExit') as string}
              value={status()?.exit_node}
              mono
            />
            <InfoRow label={t('tailscaleExitState') as string} value={status()?.exit_state} />
            <InfoRow label={t('tailscaleActualEgress') as string} value={status()?.egress} />
            <InfoRow label={t('tailscaleLastError') as string} value={status()?.last_error} />
          </div>
        </StaticConfigBlock>
        <StaticConfigBlock title={t('tailscaleSectionSettings') as string}>
          <div class="grid gap-3 pt-2 sm:grid-cols-2">
            <Show
              when={isGroupLoaded('tailscale')}
              fallback={
                <Show when={tab.loading()}>
                  <div class="py-3 text-sm text-[var(--color-text-muted)] sm:col-span-2">
                    {t('statusLoading')}
                  </div>
                </Show>
              }
            >
              <div class="flex items-start sm:col-span-2">
                <Switch
                  checked={tab.form.tailscale_enabled}
                  onChange={(checked) => tab.setForm('tailscale_enabled', checked)}
                  label={t('tailscaleEnabled') as string}
                  hint={t('tailscaleEnabledHint') as string}
                />
              </div>
              <TextInput
                type="password"
                full
                label={t('tailscaleAuthKey')}
                autocomplete="new-password"
                placeholder={t('tailscaleAuthKeyPlaceholder') as string}
                hint={
                  authKeyConfigured()
                    ? (t('tailscaleAuthKeyConfigured') as string)
                    : (t('tailscaleAuthKeyNotConfigured') as string)
                }
                value={tab.form.tailscale_auth_key}
                onInput={(event) => tab.setForm('tailscale_auth_key', event.currentTarget.value)}
              />
              <TextInput
                label={t('tailscaleHostname')}
                placeholder={t('tailscaleHostnamePlaceholder') as string}
                value={tab.form.tailscale_hostname}
                onInput={(event) => tab.setForm('tailscale_hostname', event.currentTarget.value)}
              />
              <TextInput
                label={t('tailscaleLoginServer')}
                placeholder={t('tailscaleLoginServerPlaceholder') as string}
                value={tab.form.tailscale_login_server}
                onInput={(event) =>
                  tab.setForm('tailscale_login_server', event.currentTarget.value)
                }
              />
            </Show>
            <SelectInput
              label={t('tailscaleExitNode')}
              hint={
                mutatingExitNode()
                  ? (t('tailscaleExitNodeSwitching') as string)
                  : (t('tailscaleExitNodeHint') as string)
              }
              error={mutationError() ?? exitNodesError() ?? undefined}
              value={tab.form.tailscale_exit_node}
              disabled={mutatingExitNode()}
              onChange={(event) => void handleExitNodeChange(event.currentTarget.value)}
            >
              <option value="">{t('tailscaleExitNodeNone') as string}</option>
              <Show when={tab.form.tailscale_exit_node && !selectedExitIsListed()}>
                <option value={tab.form.tailscale_exit_node} disabled>
                  {tab.form.tailscale_exit_node} · {t('tailscaleExitNodeUnavailable') as string}
                </option>
              </Show>
              <For each={exitNodes()}>
                {(node) => (
                  <option value={node.ip} disabled={!node.online}>
                    {exitNodeLabel(node)}
                  </option>
                )}
              </For>
            </SelectInput>
            <Show when={exitNodesLoaded() && exitNodes().length === 0 && !exitNodesError()}>
              <p class="text-[0.75rem] text-[var(--color-text-muted)] sm:col-span-2">
                {t('tailscaleExitNodeEmpty')}
              </p>
            </Show>
            <Show when={isGroupLoaded('tailscale')}>
              <TextInput
                type="number"
                min="1"
                max="64"
                label={t('tailscaleMaxPeers')}
                hint={t('tailscaleMaxPeersHint') as string}
                value={tab.form.tailscale_max_peers}
                onInput={(event) => {
                  setValidationError(null);
                  tab.setForm('tailscale_max_peers', event.currentTarget.value);
                }}
              />
            </Show>
          </div>
        </StaticConfigBlock>
      </div>
      <Show when={isGroupLoaded('tailscale')}>
        <SavePanel
          dirty={tab.dirty()}
          saving={tab.saving()}
          onSave={() => handleSave().catch(() => undefined)}
          onDiscard={() => {
            setValidationError(null);
            tab.discard();
          }}
          note={tab.dirty() ? (t('restartHint') as string) : undefined}
        />
      </Show>
    </TabShell>
  );
};
