import { cleanup, fireEvent, render, screen, waitFor } from '@solidjs/testing-library';
import { createStore } from 'solid-js/store';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

const api = vi.hoisted(() => ({
  fetchStatus: vi.fn(),
  fetchExitNodes: vi.fn(),
  setExitNode: vi.fn(),
  clearExitNode: vi.fn(),
}));

const config = vi.hoisted(() => ({
  values: {
    tailscale_enabled: 'true',
    tailscale_auth_key: '',
    tailscale_hostname: 'esp-claw',
    tailscale_login_server: '',
    tailscale_exit_node: '',
    tailscale_max_peers: '16',
    tailscale_auth_key_set: true,
  },
  loaded: true,
  reload: vi.fn(),
}));

vi.mock('../api/client', async (importOriginal) => ({
  ...(await importOriginal<typeof import('../api/client')>()),
  fetchTailscaleStatus: api.fetchStatus,
  fetchTailscaleExitNodes: api.fetchExitNodes,
  setTailscaleExitNode: api.setExitNode,
  clearTailscaleExitNode: api.clearExitNode,
}));

vi.mock('../state/config', () => ({
  appConfig: () => config.values,
  isGroupLoaded: () => config.loaded,
  patchConfigLocal: (patch: Record<string, unknown>) => Object.assign(config.values, patch),
  reloadConfigGroups: config.reload,
}));

vi.mock('../state/configTab', () => ({
  createConfigTab: (options: {
    toForm: (value: typeof config.values) => Record<string, unknown>;
  }) => {
    const [form, setForm] = createStore(options.toForm(config.values));
    const baseline = JSON.stringify(form);
    return {
      form,
      setForm,
      dirty: () => JSON.stringify(form) !== baseline,
      loading: () => !config.loaded,
      saving: () => false,
      error: () => null,
      save: vi.fn(),
      discard: vi.fn(),
      reload: () => config.reload(['tailscale']),
    };
  },
}));

vi.mock('../state/toast', () => ({ pushToast: vi.fn() }));

import { TailscalePage } from './TailscalePage';

const connectedStatus = {
  enabled: true,
  connected: true,
  vpn_ip: '100.87.150.122',
  path: 'derp',
  peer_count: 3,
  peer_online: 2,
  exit_node: '',
  exit_state: 'disabled',
  egress: 'sta',
  last_error: '',
  auth_key_set: true,
  heap_internal_free: 1000,
  heap_internal_largest: 500,
  heap_psram_free: 2000,
};

const onlineNode = {
  ip: '100.64.0.10',
  hostname: 'racknerd',
  online: true,
  direct: false,
  derp_region: 17,
};

const offlineNode = {
  ip: '100.64.0.11',
  hostname: 'offline-box',
  online: false,
  direct: false,
  derp_region: 0,
};

describe('TailscalePage Exit Node control', () => {
  beforeEach(() => {
    localStorage.setItem('esp-claw-lang', 'en');
    config.loaded = true;
    config.values.tailscale_exit_node = '';
    api.fetchStatus.mockResolvedValue(connectedStatus);
    api.fetchExitNodes.mockResolvedValue([]);
    api.setExitNode.mockResolvedValue({ ok: true, persisted: true });
    api.clearExitNode.mockResolvedValue({ ok: true, persisted: true });
    config.reload.mockResolvedValue(undefined);
  });

  afterEach(() => {
    cleanup();
    vi.clearAllMocks();
  });

  it('always renders a regular Wi-Fi option when no nodes are available', async () => {
    render(() => <TailscalePage />);

    expect(await screen.findByLabelText('Exit Node')).not.toBeNull();
    expect(screen.getByRole('option', { name: /regular Wi-Fi/i })).not.toBeNull();
  });

  it('keeps the Exit Node control visible while configuration is loading', async () => {
    config.loaded = false;
    render(() => <TailscalePage />);

    expect(await screen.findByLabelText('Exit Node')).not.toBeNull();
    expect(screen.getByRole('option', { name: /regular Wi-Fi/i })).not.toBeNull();
  });

  it('renders online nodes as selectable and offline nodes as disabled', async () => {
    api.fetchExitNodes.mockResolvedValue([onlineNode, offlineNode]);
    render(() => <TailscalePage />);

    const online = await screen.findByRole('option', { name: /racknerd/i });
    const offline = screen.getByRole('option', { name: /offline-box/i });
    expect((online as HTMLOptionElement).disabled).toBe(false);
    expect((offline as HTMLOptionElement).disabled).toBe(true);
  });

  it('keeps the select visible and shows an inline list error', async () => {
    api.fetchExitNodes.mockRejectedValue(new Error('Exit Node list unavailable'));
    render(() => <TailscalePage />);

    expect(await screen.findByLabelText('Exit Node')).not.toBeNull();
    expect(await screen.findByText('Exit Node list unavailable')).not.toBeNull();
  });

  it('switches immediately, disables only while mutating, then refreshes runtime and config', async () => {
    let finishMutation: ((value: unknown) => void) | undefined;
    api.fetchExitNodes.mockResolvedValue([onlineNode]);
    api.setExitNode.mockReturnValue(
      new Promise((resolve) => {
        finishMutation = resolve;
      }),
    );
    render(() => <TailscalePage />);
    const select = (await screen.findByLabelText('Exit Node')) as HTMLSelectElement;
    await screen.findByRole('option', { name: /racknerd/i });

    fireEvent.change(select, { target: { value: onlineNode.ip } });
    expect(api.setExitNode).toHaveBeenCalledWith(onlineNode.ip);
    expect(select.disabled).toBe(true);
    expect(screen.getByText('Switching Exit Node…')).not.toBeNull();

    finishMutation?.({ ok: true, persisted: true });
    await waitFor(() => expect(select.disabled).toBe(false));
    expect(api.fetchStatus).toHaveBeenCalledTimes(2);
    expect(api.fetchExitNodes).toHaveBeenCalledTimes(2);
    expect(config.reload).toHaveBeenCalledWith(['tailscale']);
  });

  it('clears the Exit Node immediately without showing restart-needed UI', async () => {
    config.values.tailscale_exit_node = onlineNode.ip;
    api.fetchExitNodes.mockResolvedValue([onlineNode]);
    render(() => <TailscalePage />);
    const select = (await screen.findByLabelText('Exit Node')) as HTMLSelectElement;

    fireEvent.change(select, { target: { value: '' } });
    await waitFor(() => expect(api.clearExitNode).toHaveBeenCalledOnce());
    expect(
      screen.getByRole('button', { name: 'Save Tab' }).parentElement?.textContent,
    ).not.toContain('Restart the device after saving to apply new settings');
    expect(config.reload).toHaveBeenCalledWith(['tailscale']);
  });

  it('preserves the restart note for ordinary configuration edits', async () => {
    render(() => <TailscalePage />);
    const hostname = await screen.findByLabelText('Device Hostname');

    fireEvent.input(hostname, { target: { value: 'renamed-device' } });

    await waitFor(() =>
      expect(screen.getByRole('button', { name: 'Save Tab' }).parentElement?.textContent).toContain(
        'Restart the device after saving to apply new settings',
      ),
    );
  });

  it('surfaces rollback recovery after a failed switch and restores live state', async () => {
    api.fetchExitNodes.mockResolvedValue([onlineNode]);
    api.setExitNode.mockRejectedValue(
      Object.assign(new Error('Failed to switch Exit Node'), {
        operation: {
          ok: false,
          error: 'runtime_apply_failed',
          message: 'Failed to switch Exit Node',
          rollback_attempted: true,
          rollback_recovered: true,
        },
      }),
    );
    render(() => <TailscalePage />);
    const select = (await screen.findByLabelText('Exit Node')) as HTMLSelectElement;
    await screen.findByRole('option', { name: /racknerd/i });

    fireEvent.change(select, { target: { value: onlineNode.ip } });

    expect(await screen.findByText(/previous Exit Node was restored/i)).not.toBeNull();
    await waitFor(() => expect(select.disabled).toBe(false));
    expect(api.fetchStatus).toHaveBeenCalledTimes(2);
    expect(config.reload).toHaveBeenCalledWith(['tailscale']);
  });
});
