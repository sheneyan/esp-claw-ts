import { describe, expect, it } from 'vitest';
import { TAILSCALE_MAX_DERP_RTTS, type TailscaleDiagnosticStatus } from './client';

const diagnosticFixture = {
  ok: true,
  enabled: true,
  connected: true,
  hostname: 'esp-claw',
  vpn_ip: '100.87.150.122',
  path: 'derp',
  peer_count: 3,
  peer_online: 2,
  exit_node: '100.64.0.10',
  state: 'active',
  egress: 'tailscale',
  dns_egress: 'mixed',
  dns_bypass_active: true,
  dns_bypass_count: 2,
  last_error: '',
  derp: {
    active: { id: 17, name: 'Seattle' },
    default: { id: 1, name: 'New York City' },
    rtt_count: 2,
    rtts: [
      { region: { id: 17, name: 'Seattle' }, rtt_ms: 42, timed_out: false },
      { region: { id: 9, name: 'Tokyo' }, rtt_ms: 0, timed_out: true },
    ],
  },
  timing: {
    derp_heartbeat_age_ms: 1234,
    control_rx_age_ms: 5678,
  },
  reconnect: {
    coord_watchdog: 1,
    coord_transport: 2,
    derp_watchdog: 3,
    derp_retry: 4,
  },
} satisfies TailscaleDiagnosticStatus;

describe('Tailscale diagnostic response contract', () => {
  it('matches the bounded backend DERP, timing, and reconnect payload', () => {
    expect(diagnosticFixture.derp.active).toEqual({ id: 17, name: 'Seattle' });
    expect(diagnosticFixture.derp.rtts).toHaveLength(diagnosticFixture.derp.rtt_count);
    expect(diagnosticFixture.derp.rtts.length).toBeLessThanOrEqual(TAILSCALE_MAX_DERP_RTTS);
    expect(diagnosticFixture.timing.control_rx_age_ms).toBe(5678);
    expect(diagnosticFixture.reconnect.derp_retry).toBe(4);
  });
});
