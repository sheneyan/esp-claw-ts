import { cleanup, render, waitFor } from '@solidjs/testing-library';
import { afterEach, describe, expect, it, vi } from 'vitest';

const state = vi.hoisted(() => ({
  config: {
    tailscale_hostname: 'esp-claw',
    tailscale_auth_key: '',
    tailscale_exit_node: '',
  },
}));

vi.mock('./config', () => ({
  appConfig: () => state.config,
  ensureConfigGroups: vi.fn().mockResolvedValue(undefined),
  isGroupLoaded: () => true,
  patchConfigLocal: vi.fn(),
  reloadConfigGroups: vi.fn().mockResolvedValue(undefined),
}));
vi.mock('./dirty', () => ({ markDirty: vi.fn() }));
vi.mock('./toast', () => ({ pushToast: vi.fn() }));
vi.mock('../api/client', () => ({ saveConfigPatch: vi.fn().mockResolvedValue({ ok: true }) }));

import { createConfigTab, type ConfigTabApi } from './configTab';

type TestForm = {
  tailscale_hostname: string;
  tailscale_auth_key: string;
  tailscale_exit_node: string;
};

describe('createConfigTab live field merging', () => {
  afterEach(cleanup);

  it('updates one live baseline field without discarding unrelated drafts', async () => {
    let tab: ConfigTabApi<TestForm> | undefined;
    render(() => {
      tab = createConfigTab<TestForm>({
        tab: 'tailscale',
        groups: ['tailscale'],
        toForm: (config) => ({
          tailscale_hostname: config.tailscale_hostname ?? '',
          tailscale_auth_key: '',
          tailscale_exit_node: config.tailscale_exit_node ?? '',
        }),
        fromForm: (form) => form,
      });
      return null;
    });
    await waitFor(() => expect(tab).toBeDefined());
    await new Promise((resolve) => setTimeout(resolve, 0));
    await waitFor(() => expect(tab!.loading()).toBe(false));

    tab!.setForm('tailscale_hostname', 'draft-host');
    tab!.setForm('tailscale_auth_key', 'draft-auth');
    expect(tab!.dirty()).toBe(true);

    state.config.tailscale_exit_node = '100.64.0.10';
    tab!.mergeLiveFields({ tailscale_exit_node: '100.64.0.10' });

    expect(tab!.form.tailscale_hostname).toBe('draft-host');
    expect(tab!.form.tailscale_auth_key).toBe('draft-auth');
    expect(tab!.form.tailscale_exit_node).toBe('100.64.0.10');
    expect(tab!.dirty()).toBe(true);

    tab!.discard();
    expect(tab!.form).toMatchObject({
      tailscale_hostname: 'esp-claw',
      tailscale_auth_key: '',
      tailscale_exit_node: '100.64.0.10',
    });
    expect(tab!.dirty()).toBe(false);
  });
});
