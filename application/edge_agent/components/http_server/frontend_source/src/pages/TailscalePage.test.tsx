import { cleanup, fireEvent, render, screen, waitFor, within } from '@solidjs/testing-library';
import { createStore } from 'solid-js/store';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

const api = vi.hoisted(() => ({
  fetchStatus: vi.fn(),
  fetchExitNodes: vi.fn(),
  fetchConfigGroup: vi.fn(),
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
  setLoaded(_value: boolean): void {},
  notifyValues(): void {},
  reload: vi.fn(),
  save: vi.fn(),
  discard: vi.fn(),
}));

vi.mock('../api/client', async (importOriginal) => ({
  ...(await importOriginal<typeof import('../api/client')>()),
  fetchTailscaleStatus: api.fetchStatus,
  fetchTailscaleExitNodes: api.fetchExitNodes,
  fetchConfigGroups: api.fetchConfigGroup,
  setTailscaleExitNode: api.setExitNode,
  clearTailscaleExitNode: api.clearExitNode,
}));

vi.mock('../state/config', async () => {
  const { createSignal } = await import('solid-js');
  const [loaded, setLoaded] = createSignal(config.loaded);
  const [valuesVersion, setValuesVersion] = createSignal(0);
  config.setLoaded = (value: boolean) => {
    config.loaded = value;
    setLoaded(value);
  };
  config.notifyValues = () => {
    setValuesVersion((version) => version + 1);
  };
  return {
    appConfig: () => {
      valuesVersion();
      return config.values;
    },
    isGroupLoaded: () => loaded(),
    patchConfigLocal: (patch: Record<string, unknown>) => {
      Object.assign(config.values, patch);
      setValuesVersion((version) => version + 1);
    },
    reloadConfigGroups: config.reload,
  };
});

vi.mock('../state/configTab', () => ({
  createConfigTab: (options: {
    toForm: (value: typeof config.values) => Record<string, unknown>;
  }) => {
    const [form, setForm] = createStore(options.toForm(config.values));
    let baseline = { ...form };
    return {
      form,
      setForm,
      dirty: () => JSON.stringify(form) !== JSON.stringify(baseline),
      loading: () => !config.loaded,
      saving: () => false,
      error: () => null,
      save: config.save,
      discard: () => {
        config.discard();
        setForm({ ...baseline });
      },
      reload: () => config.reload(['tailscale']),
      mergeLiveFields: (patch: Record<string, unknown>) => {
        baseline = { ...baseline, ...patch };
        setForm(patch);
      },
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
  dns_egress: 'sta',
  dns_bypass_active: false,
  dns_bypass_count: 0,
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
    config.setLoaded(true);
    config.values.tailscale_hostname = 'esp-claw';
    config.values.tailscale_auth_key = '';
    config.values.tailscale_login_server = '';
    config.values.tailscale_exit_node = '';
    api.fetchStatus.mockResolvedValue(connectedStatus);
    api.fetchExitNodes.mockResolvedValue([]);
    api.fetchConfigGroup.mockImplementation(() =>
      Promise.resolve({ tailscale_exit_node: config.values.tailscale_exit_node }),
    );
    api.setExitNode.mockResolvedValue({ ok: true, persisted: true, selected_ip: onlineNode.ip });
    api.clearExitNode.mockResolvedValue({ ok: true, persisted: true, selected_ip: '' });
    config.reload.mockResolvedValue(undefined);
  });

  afterEach(() => {
    cleanup();
    vi.clearAllMocks();
  });

  it('always renders a regular Wi-Fi option when no nodes are available', async () => {
    render(() => <TailscalePage />);

    expect(await screen.findByLabelText('Exit Node')).toBeInTheDocument();
    expect(screen.getByRole('option', { name: /regular Wi-Fi/i })).toBeInTheDocument();
    expect(await screen.findByText('No Exit Nodes available.')).toBeInTheDocument();
  });

  it('shows the local-DNS compatibility warning while Exit Node DNS bypass is active', async () => {
    api.fetchStatus.mockResolvedValue({
      ...connectedStatus,
      exit_node: onlineNode.ip,
      exit_state: 'active',
      egress: 'exit',
      dns_egress: 'sta_bypass',
      dns_bypass_active: true,
      dns_bypass_count: 2,
    });
    render(() => <TailscalePage />);

    expect(await screen.findByText('Local Wi-Fi DNS (2 public bypasses)')).toBeInTheDocument();
    expect(
      screen.getByText(/DNS queries use local Wi-Fi and may be visible to the local resolver or ISP/i),
    ).toBeInTheDocument();
  });

  it('discloses local DNS for an active Exit Node with only private resolvers', async () => {
    api.fetchStatus.mockResolvedValue({
      ...connectedStatus,
      exit_node: onlineNode.ip,
      exit_state: 'active',
      egress: 'exit',
      dns_egress: 'sta',
      dns_bypass_active: false,
      dns_bypass_count: 0,
    });
    render(() => <TailscalePage />);

    expect(await screen.findByText('Local Wi-Fi DNS (0 public bypasses)')).toBeInTheDocument();
    expect(
      screen.getByText(/DNS queries use local Wi-Fi and may be visible to the local resolver or ISP/i),
    ).toBeInTheDocument();
  });

  it('does not show a local-DNS warning during ordinary STA egress', async () => {
    render(() => <TailscalePage />);

    await screen.findByText('DNS Egress');
    expect(
      screen.queryByText(/DNS queries use local Wi-Fi and may be visible to the local resolver or ISP/i),
    ).not.toBeInTheDocument();
  });

  it('keeps the Exit Node control visible while configuration is loading', async () => {
    config.setLoaded(false);
    render(() => <TailscalePage />);

    expect(await screen.findByLabelText('Exit Node')).toBeInTheDocument();
    expect(screen.getByRole('option', { name: /regular Wi-Fi/i })).toBeInTheDocument();
  });

  it('renders online nodes as selectable and offline nodes as disabled', async () => {
    api.fetchExitNodes.mockResolvedValue([onlineNode, offlineNode]);
    render(() => <TailscalePage />);

    const online = await screen.findByRole('option', { name: /racknerd/i });
    const offline = screen.getByRole('option', { name: /offline-box/i });
    expect(online).toBeEnabled();
    expect(offline).toBeDisabled();
    expect(screen.queryByText('No Exit Nodes available.')).not.toBeInTheDocument();
  });

  it('keeps the select visible and shows an inline list error', async () => {
    api.fetchExitNodes.mockRejectedValue(new Error('Exit Node list unavailable'));
    render(() => <TailscalePage />);

    expect(await screen.findByLabelText('Exit Node')).toBeInTheDocument();
    expect(await screen.findByText('Exit Node list unavailable')).toBeInTheDocument();
    expect(screen.getByRole('alert')).toHaveTextContent('Exit Node list unavailable');
    expect(screen.getByLabelText('Exit Node')).toHaveAttribute('aria-invalid', 'true');
    expect(screen.queryByText('No Exit Nodes available.')).not.toBeInTheDocument();
  });

  it('switches immediately, disables only while mutating, then refreshes runtime and config', async () => {
    let finishMutation: ((value: unknown) => void) | undefined;
    api.fetchExitNodes.mockResolvedValue([onlineNode]);
    api.fetchConfigGroup.mockResolvedValue({ tailscale_exit_node: onlineNode.ip });
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
    expect(select).toBeDisabled();
    expect(screen.getByRole('status')).toHaveTextContent('Switching Exit Node…');

    finishMutation?.({ ok: true, persisted: true, selected_ip: onlineNode.ip });
    await waitFor(() => expect(select).toBeEnabled());
    expect(screen.getByRole('status')).toHaveTextContent('Exit Node selection updated.');
    expect(api.fetchStatus).toHaveBeenCalledTimes(2);
    expect(api.fetchExitNodes).toHaveBeenCalledTimes(2);
    expect(api.fetchConfigGroup).toHaveBeenCalledWith(['tailscale']);
  });

  it('clears the Exit Node immediately without showing restart-needed UI', async () => {
    config.values.tailscale_exit_node = onlineNode.ip;
    api.fetchExitNodes.mockResolvedValue([onlineNode]);
    api.fetchConfigGroup.mockResolvedValue({ tailscale_exit_node: '' });
    render(() => <TailscalePage />);
    const select = (await screen.findByLabelText('Exit Node')) as HTMLSelectElement;

    fireEvent.change(select, { target: { value: '' } });
    await waitFor(() => expect(api.clearExitNode).toHaveBeenCalledOnce());
    await waitFor(() => expect(select).toBeEnabled());
    expect(select).toHaveValue('');
    expect(
      screen.getByRole('button', { name: 'Save Tab' }).parentElement?.textContent,
    ).not.toContain('Restart the device after saving to apply new settings');
    expect(api.fetchConfigGroup).toHaveBeenCalledWith(['tailscale']);
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

  it('preserves drafts and serializes Save and Discard during a live switch', async () => {
    let finishMutation: ((value: unknown) => void) | undefined;
    api.fetchExitNodes.mockResolvedValue([onlineNode]);
    api.fetchConfigGroup.mockResolvedValue({
      tailscale_exit_node: onlineNode.ip,
      tailscale_hostname: 'server-host-that-must-not-replace-draft',
      tailscale_login_server: 'https://server-control.example',
    });
    api.setExitNode.mockReturnValue(
      new Promise((resolve) => {
        finishMutation = resolve;
      }),
    );
    render(() => <TailscalePage />);
    const select = (await screen.findByLabelText('Exit Node')) as HTMLSelectElement;
    await screen.findByRole('option', { name: /racknerd/i });
    const hostname = screen.getByLabelText('Device Hostname') as HTMLInputElement;
    const authKey = screen.getByLabelText('Auth Key') as HTMLInputElement;
    const loginServer = screen.getByLabelText('Login Server') as HTMLInputElement;

    fireEvent.input(hostname, { target: { value: 'draft-host' } });
    fireEvent.input(authKey, { target: { value: 'draft-auth-key' } });
    fireEvent.input(loginServer, { target: { value: 'https://draft-control.example' } });
    fireEvent.change(select, { target: { value: onlineNode.ip } });

    const discardButton = screen.getByRole('button', { name: 'Discard' });
    const savePanel = discardButton.parentElement!;
    const saveButton = within(savePanel).getAllByRole('button')[1]!;
    expect(saveButton).toBeDisabled();
    expect(discardButton).toBeDisabled();
    fireEvent.click(saveButton);
    fireEvent.click(discardButton);
    expect(config.save).not.toHaveBeenCalled();
    expect(config.discard).not.toHaveBeenCalled();
    finishMutation?.({ ok: true, persisted: true, selected_ip: onlineNode.ip });
    await waitFor(() => expect(select).toBeEnabled());

    expect(hostname).toHaveValue('draft-host');
    expect(authKey).toHaveValue('draft-auth-key');
    expect(loginServer).toHaveValue('https://draft-control.example');
    expect(screen.getByRole('button', { name: 'Save Tab' })).toBeEnabled();
    expect(screen.getByRole('button', { name: 'Discard' })).toBeEnabled();
    expect(screen.getByRole('button', { name: 'Save Tab' }).parentElement).toHaveTextContent(
      'Restart the device after saving to apply new settings',
    );
  });

  it('keeps the confirmed selection over a delayed stale initial config response', async () => {
    config.setLoaded(false);
    api.fetchExitNodes.mockResolvedValue([onlineNode]);
    api.fetchConfigGroup.mockRejectedValue(new Error('Config refresh failed'));
    render(() => <TailscalePage />);
    const select = (await screen.findByLabelText('Exit Node')) as HTMLSelectElement;
    await screen.findByRole('option', { name: /racknerd/i });

    fireEvent.change(select, { target: { value: onlineNode.ip } });

    await waitFor(() => expect(select).toBeEnabled());
    expect(select).toHaveValue(onlineNode.ip);
    expect(screen.getByRole('status')).toHaveTextContent('Exit Node selection updated.');
    expect(screen.getByRole('alert')).toHaveTextContent(
      'Saved selection could not be verified from configuration.',
    );

    config.values.tailscale_exit_node = '';
    config.notifyValues();
    config.setLoaded(true);
    await Promise.resolve();

    expect(select).toHaveValue(onlineNode.ip);
    expect(screen.getByRole('alert')).toHaveTextContent(
      'Saved selection could not be verified from configuration.',
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

    expect(await screen.findByText(/previous Exit Node was restored/i)).toBeInTheDocument();
    expect(screen.getByRole('alert')).toHaveTextContent('previous Exit Node was restored');
    await waitFor(() => expect(select).toBeEnabled());
    expect(api.fetchStatus).toHaveBeenCalledTimes(2);
    expect(api.fetchConfigGroup).toHaveBeenCalledWith(['tailscale']);
  });
});
