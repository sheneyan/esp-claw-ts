import { cleanup, fireEvent, render, screen, waitFor } from '@solidjs/testing-library';
import { createStore } from 'solid-js/store';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

const api = vi.hoisted(() => ({
  fetchProfiles: vi.fn(),
  saveProfiles: vi.fn(),
  connectProfile: vi.fn(),
}));

vi.mock('../api/client', async (importOriginal) => ({
  ...(await importOriginal<typeof import('../api/client')>()),
  fetchWifiProfiles: api.fetchProfiles,
  saveWifiProfiles: api.saveProfiles,
  connectWifiProfile: api.connectProfile,
}));

vi.mock('../state/config', () => ({
  appStatus: () => ({ ap_ssid: 'ESP-Claw-Test' }),
}));

vi.mock('../state/configTab', () => ({
  createConfigTab: (options: {
    toForm: (config: Record<string, string>) => Record<string, string>;
  }) => {
    const [form, setForm] = createStore(
      options.toForm({ ap_ssid: '', ap_password: '', ap_behavior: 'keep', time_timezone: '' }),
    );
    return {
      form,
      setForm,
      dirty: () => false,
      saving: () => false,
      error: () => null,
      save: vi.fn(),
      discard: vi.fn(),
    };
  },
}));

vi.mock('../state/toast', () => ({ pushToast: vi.fn() }));

import { BasicPage } from './BasicPage';

const profile = (index: number, ssid: string, active = false) => ({
  index,
  ssid,
  configured: true,
  active,
  password_set: true,
});

describe('BasicPage saved Wi-Fi profiles', () => {
  beforeEach(() => {
    api.fetchProfiles.mockResolvedValue([profile(0, 'Office', true), profile(1, 'Phone')]);
    api.saveProfiles.mockResolvedValue({ ok: true });
    api.connectProfile.mockResolvedValue({ accepted: true });
  });

  afterEach(() => {
    cleanup();
    vi.clearAllMocks();
    vi.restoreAllMocks();
  });

  it('never fills a returned saved password and enforces five rows', async () => {
    api.fetchProfiles.mockResolvedValue(
      Array.from({ length: 5 }, (_, index) => profile(index, `WiFi-${index}`)),
    );
    render(() => <BasicPage onRestartRequest={() => undefined} />);

    const passwordInputs = (await screen.findAllByLabelText(
      'Wi-Fi Password',
    )) as HTMLInputElement[];
    expect(passwordInputs).toHaveLength(5);
    expect(passwordInputs.every((input) => input.value === '')).toBe(true);
    expect(screen.getByRole('button', { name: 'Add network' })).toBeDisabled();
  });

  it('saves reordered profiles and confirms an explicit switch', async () => {
    const confirm = vi.spyOn(window, 'confirm').mockReturnValue(true);
    render(() => <BasicPage onRestartRequest={() => undefined} />);
    await screen.findByDisplayValue('Office');

    fireEvent.click(screen.getAllByRole('button', { name: 'Up' })[1]!);
    fireEvent.click(screen.getByRole('button', { name: 'Save Wi-Fi Networks' }));
    await waitFor(() => expect(api.saveProfiles).toHaveBeenCalled());
    expect(api.saveProfiles.mock.calls[0]![0].map((item: { ssid: string }) => item.ssid)).toEqual([
      'Phone',
      'Office',
    ]);

    await waitFor(() =>
      expect(screen.getAllByRole('button', { name: 'Connect now' })[0]).toBeEnabled(),
    );
    fireEvent.click(screen.getAllByRole('button', { name: 'Connect now' })[0]!);
    expect(confirm).toHaveBeenCalledOnce();
    await waitFor(() => expect(api.connectProfile).toHaveBeenCalledWith(0));
  });
});
