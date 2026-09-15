#!/usr/bin/env python3
"""Summarize Panthera boot verification logs."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Check:
    name: str
    result: str
    detail: str
    required: bool = True


def has(pattern: str, text: str) -> bool:
    return re.search(pattern, text, re.MULTILINE) is not None


def marker_statuses(text: str) -> dict[str, int]:
    statuses: dict[str, int] = {}
    for name, status in re.findall(r"__PANTHERA_STATUS_([A-Za-z0-9_]+):(-?[0-9]+)__", text):
        statuses[name] = int(status)
    return statuses


def command_check(
    statuses: dict[str, int],
    name: str,
    detail: str,
    *,
    text: str,
    output_pattern: str | None = None,
    required: bool = True,
    expected_fail: bool = False,
) -> Check:
    if name not in statuses:
        return Check(name, "FAIL" if required else "EXPECTED-FAIL", "status marker missing", required)
    status = statuses[name]
    if status == 0 and (output_pattern is None or has(output_pattern, text)):
        return Check(name, "PASS", detail, required)
    if expected_fail:
        return Check(name, "EXPECTED-FAIL", f"exit status {status} or expected output missing", False)
    if output_pattern is not None and status == 0:
        return Check(name, "FAIL", f"expected output missing: {output_pattern}", required)
    return Check(name, "FAIL", f"exit status {status}", required)


def build_checks(
    text: str,
    verify_text: str | None,
    strict_dns: bool,
    strict_network: bool,
    strict_configd: bool,
    strict_ipconfiguration: bool,
    strict_ipconfiguration_prime: bool,
    strict_ipconfiguration_dhcp: bool,
    strict_ipconfiguration_dhcp_packet: bool,
    strict_ipconfiguration_boot: bool,
    strict_ipconfiguration_network: bool,
    strict_dispatch_timer: bool,
    strict_mdns: bool,
) -> list[Check]:
    statuses = marker_statuses(text)
    ipconfiguration_prime_started = has(
        r"PANTHERA:IPConfiguration (prime queued|prime enter|handle prime enter)",
        text,
    )
    dhcp_packet_sent = (
        has(r"PANTHERA:KERN sendto_nocancel sendit returned error=0 retval=300", text)
        or has(r"PANTHERA:UDP socket send returned status=300", text)
        or (
            has(r"PANTHERA:DHCP init transmit returned ok", text)
            and not has(r"PANTHERA:UDP socket send returned status=(4294967295|-1)", text)
            and not has(r"PANTHERA:KERN .* returned error=(49|51|65)", text)
        )
    )
    require_prime_unwind = strict_ipconfiguration_prime and not strict_ipconfiguration_dhcp_packet
    require_dhcp_unwind = strict_ipconfiguration_dhcp and not strict_ipconfiguration_dhcp_packet
    daemon_network_store_passed = statuses.get("ipconfiguration_daemon_sc_probe") == 0
    daemon_dns_passed = (
        statuses.get("ipconfiguration_daemon_dns_tcp") == 0
        and has(r"dnsprobe: resolved|dnsprobe: tcp connect ok", text)
    )
    checks = [
        Check(
            "efi_handoff",
            "PASS" if has(r"Panthera BOOTX64 scaffold loaded", text) else "FAIL",
            "EFI loader banner present",
        ),
        Check(
            "kernel_started",
            "PASS" if has(r"Darwin Kernel Version", text) else "FAIL",
            "kernel version banner present",
        ),
        Check(
            "root_mounted",
            "PASS" if has(r"hfs: mounted PantheraRoot|BSD root:", text) else "FAIL",
            "root filesystem attached and mounted",
        ),
        Check(
            "launchd_pid1",
            "PASS" if has(r"load_init_program: attempting to load /sbin/launchd", text) else "FAIL",
            "kernel loaded /sbin/launchd",
        ),
        Check(
            "launchd_jobs",
            "PASS" if has(r"launchd: loaded [0-9]+ jobs", text) else "FAIL",
            "launchd imported plist jobs",
        ),
        Check(
            "login_prompt",
            "PASS" if has(r"login:", text) else "FAIL",
            "login prompt reached",
        ),
        Check(
            "shell_prompt",
            "PASS" if has(r"root@panthera:|panthera# ", text) else "FAIL",
            "interactive shell prompt reached",
        ),
        command_check(
            statuses,
            "external_command",
            "guest shell executed /bin/echo",
            text=text,
            output_pattern=r"PANTHERA_BOOT_VERIFY_EXEC",
        ),
        command_check(
            statuses,
            "id",
            "guest verifier confirmed root identity",
            text=text,
            output_pattern=r"uid=0\(root\)",
        ),
        command_check(
            statuses,
            "cf_dictionary",
            "CoreFoundation dictionary smoke test completed",
            text=text,
            output_pattern=r"CoreFoundation: ALL PASS|CFDictionary count:",
        ),
        command_check(
            statuses,
            "net_flags",
            "static interface flags completed",
            text=text,
            output_pattern=r"PANTHERA_NETPROBE_STAGE:flags:(after_set|done)",
            required=strict_network,
            expected_fail=not strict_network,
        )
        if "net_flags" in statuses
        else Check(
            "net_flags",
            "EXPECTED-FAIL" if not strict_network else "FAIL",
            "status marker missing",
            strict_network,
        ),
        command_check(
            statuses,
            "netbringup",
            "static network bring-up completed",
            text=text,
            output_pattern=r"netbringup: (success|configured; dns probe failed|interface up|address set)|PANTHERA_NETPROBE_STAGE:addr:before_ioctl",
            required=strict_network,
            expected_fail=not strict_network,
        ),
        command_check(
            statuses,
            "net_route",
            "default route publication completed",
            text=text,
            output_pattern=r"PANTHERA_NETPROBE_STAGE:route:before_write",
            required=strict_network,
            expected_fail=not strict_network,
        )
        if "net_route" in statuses
        else Check(
            "net_route",
            "EXPECTED-FAIL" if not strict_network else "FAIL",
            "status marker missing",
            strict_network,
        ),
        command_check(
            statuses,
            "net_arp",
            "ARP transmit path enabled after static address setup",
            text=text,
            output_pattern=r"PANTHERA_NETPROBE_STAGE:arp_on:(after_set|done)",
            required=strict_network,
            expected_fail=not strict_network,
        )
        if "net_arp" in statuses
        else Check(
            "net_arp",
            "EXPECTED-FAIL" if not strict_network else "FAIL",
            "status marker missing",
            strict_network,
        ),
        command_check(
            statuses,
            "net_proto",
            "IPv4 protocol attachment completed after static setup",
            text=text,
            output_pattern=r"PANTHERA_NETPROBE_STAGE:proto_attach:(after_ioctl|done)",
            required=strict_network,
            expected_fail=not strict_network,
        )
        if "net_proto" in statuses
        else Check(
            "net_proto",
            "EXPECTED-FAIL" if not strict_network else "FAIL",
            "status marker missing",
            strict_network,
        ),
        command_check(
            statuses,
            "net_addr_finalize",
            "normal IPv4 address finalization completed after ARP/proto attach",
            text=text,
            output_pattern=r"PANTHERA_NETPROBE_STAGE:addr:before_ioctl",
            required=strict_network,
            expected_fail=not strict_network,
        )
        if "net_addr_finalize" in statuses
        else Check(
            "net_addr_finalize",
            "EXPECTED-FAIL" if not strict_network else "FAIL",
            "status marker missing",
            strict_network,
        ),
        command_check(
            statuses,
            "net_static_arp",
            "diagnostic static gateway link-layer route insertion",
            text=text,
            output_pattern=r"PANTHERA_NETPROBE_STAGE:arp:(after_write|done)",
            required=False,
            expected_fail=True,
        )
        if "net_static_arp" in statuses
        else Check(
            "net_static_arp",
            "EXPECTED-FAIL",
            "status marker missing",
            False,
        ),
        command_check(
            statuses,
            "net_eflags",
            "interface extended flags snapshot completed",
            text=text,
            output_pattern=r"PANTHERA_NETPROBE_EFLAGS:0x[0-9a-fA-F]+",
            required=strict_network,
            expected_fail=not strict_network,
        )
        if "net_eflags" in statuses
        else Check(
            "net_eflags",
            "EXPECTED-FAIL" if not strict_network else "FAIL",
            "status marker missing",
            strict_network,
        ),
        command_check(
            statuses,
            "ifconfig",
            "network interface command completed",
            text=text,
            output_pattern=r"en0|10\.0\.2\.15|inet |PANTHERA_NETPROBE_STAGE:verify_addr:done",
            required=strict_network,
            expected_fail=not strict_network,
        ),
        command_check(
            statuses,
            "gateway_ping",
            "gateway ping completed",
            text=text,
            output_pattern=r"bytes from|packets received|seq=",
            required=strict_network,
            expected_fail=not strict_network,
        ),
        command_check(
            statuses,
            "netstat_after_ping",
            "route table snapshot after gateway ping completed",
            text=text,
            output_pattern=r"Destination\s+Gateway\s+Flags",
            required=strict_network,
            expected_fail=not strict_network,
        )
        if "netstat_after_ping" in statuses
        else Check(
            "netstat_after_ping",
            "EXPECTED-FAIL" if not strict_network else "FAIL",
            "status marker missing",
            strict_network,
        ),
        command_check(
            statuses,
            "dns_tcp",
            "DNS-over-TCP probe completed",
            text=text,
            output_pattern=r"dnsprobe: resolved|dnsprobe: tcp connect ok",
            required=strict_dns,
            expected_fail=not strict_dns,
        ),
        command_check(
            statuses,
            "bootstrap",
            "bootstrap Mach smoke test completed",
            text=text,
            output_pattern=r"test_bootstrap(_simple)?: PASS",
        ),
    ]

    if strict_dispatch_timer or "dispatch_timer" in statuses:
        checks.append(
            command_check(
                statuses,
                "dispatch_timer",
                "dispatch timer source callback completed",
                text=text,
                output_pattern=r"PANTHERA_DISPATCH_TIMER:FIRED",
                required=strict_dispatch_timer,
                expected_fail=not strict_dispatch_timer,
            )
        )

    if strict_mdns or "mdns_dns_sd_probe" in statuses or has(r"mDNSResponder .* starting", text):
        checks.extend(
            [
                Check(
                    "mdnsresponder_launch",
                    "PASS" if has(r"mDNSResponder .* starting", text) else "FAIL",
                    "mDNSResponder launch banner present",
                    strict_mdns,
                ),
                command_check(
                    statuses,
                    "mdns_dns_sd_probe",
                    "mDNSResponder accepted a DNS-SD IPC TCP loopback connection",
                    text=text,
                    output_pattern=r"PANTHERA_MDNS_DNSSD_PROBE:PASS",
                    required=strict_mdns,
                    expected_fail=not strict_mdns,
                ),
            ]
        )

    if strict_configd or has(r"PANTHERA:configd", text):
        checks.extend(
            [
                Check(
                    "configd_main",
                    "PASS" if has(r"PANTHERA:configd main entered", text) else "FAIL",
                    "configd reached main",
                    strict_configd,
                ),
                Check(
                    "configd_store",
                    "PASS"
                    if has(r"PANTHERA:configd (store create ok|created store ptr=)", text)
                    else "FAIL" if strict_configd else "EXPECTED-FAIL",
                    "configd created dynamic store",
                    strict_configd,
                ),
            ]
        )

    if strict_ipconfiguration or has(r"PANTHERA:IPConfiguration", text):
        checks.extend(
            [
                Check(
                    "ipconfiguration_launcher",
                    "PASS"
                    if has(r"PANTHERA:IPConfiguration launcher start", text)
                    or has(r"PANTHERA:configd IPConfiguration plugin start", text)
                    else "FAIL",
                    "IPConfiguration launcher or configd plugin started",
                    strict_ipconfiguration,
                ),
                Check(
                    "ipconfiguration_bundle",
                    "PASS"
                    if has(r"PANTHERA:(IPConfiguration|configd IPConfiguration) bundle create ok", text)
                    else "FAIL",
                    "IPConfiguration has a real CFBundle context",
                    strict_ipconfiguration,
                ),
                Check(
                    "ipconfiguration_configd_ready",
                    "PASS" if has(r"PANTHERA:IPConfiguration configd ready", text) else "FAIL",
                    "IPConfiguration connected to configd dynamic store",
                    strict_ipconfiguration,
                ),
                Check(
                    "ipconfiguration_load_enter",
                    "PASS" if has(r"PANTHERA:IPConfiguration load enter", text) else "FAIL",
                    "IPConfiguration reached traced load body",
                    strict_ipconfiguration,
                ),
                Check(
                    "ipconfiguration_load_return",
                    "PASS" if has(r"PANTHERA:IPConfiguration load returned", text) else "FAIL",
                    "IPConfiguration returned from load",
                    strict_ipconfiguration,
                ),
                Check(
                    "ipconfiguration_start_return",
                    "PASS" if has(r"PANTHERA:IPConfiguration start returned", text) else "FAIL",
                    "IPConfiguration returned from start",
                    strict_ipconfiguration,
                ),
                Check(
                    "ipconfiguration_cga_init",
                    "PASS"
                    if has(r"PANTHERA:IPConfiguration start cga init returned", text)
                    else "FAIL",
                    "IPConfiguration called normal CGA initialization",
                    strict_ipconfiguration,
                ),
                Check(
                    "ipconfiguration_control_prefs_callback",
                    "PASS"
                    if has(r"PANTHERA:IPConfiguration control prefs callback=1 queue=1", text)
                    else "FAIL",
                    "IPConfiguration scheduled live control preference callbacks",
                    strict_ipconfiguration,
                ),
                Check(
                    "ipconfiguration_dhcp_prefs_callback",
                    "PASS"
                    if has(r"PANTHERA:IPConfiguration start dhcp prefs callback=1 queue=1", text)
                    else "FAIL",
                    "IPConfiguration scheduled live DHCP preference callbacks",
                    strict_ipconfiguration,
                ),
                Check(
                    "ipconfiguration_loopback_setup",
                    "PASS"
                    if has(r"PANTHERA:IPConfiguration start loopback set returned", text)
                    else "FAIL",
                    "IPConfiguration ran normal loopback setup",
                    strict_ipconfiguration,
                ),
                Check(
                    "ipconfiguration_prime",
                    "PASS" if ipconfiguration_prime_started else "FAIL",
                    "IPConfiguration queued or entered prime work",
                    strict_ipconfiguration,
                ),
                Check(
                    "ipconfiguration_prime_async_dispatch",
                    "PASS"
                    if has(r"PANTHERA:IPConfiguration prime async dispatch returned", text)
                    else "FAIL",
                    "IPConfiguration queued prime work with dispatch_async",
                    strict_ipconfiguration,
                ),
            ]
        )

    if strict_ipconfiguration_prime or has(r"PANTHERA:IPConfiguration prime handle start", text):
        checks.extend(
            [
                Check(
                    "ipconfiguration_prime_handle_enter",
                    "PASS" if has(r"PANTHERA:IPConfiguration handle prime enter", text) else "FAIL",
                    "IPConfiguration prime thread entered",
                    strict_ipconfiguration_prime,
                ),
                Check(
                    "ipconfiguration_prime_handle_start",
                    "PASS" if has(r"PANTHERA:IPConfiguration prime handle start", text) else "FAIL",
                    "IPConfiguration handle_prime body started",
                    strict_ipconfiguration_prime,
                ),
                Check(
                    "ipconfiguration_prime_initialization_enter",
                    "PASS"
                    if has(r"PANTHERA:IPConfiguration prime (start initialization|update interfaces) enter", text)
                    else "FAIL",
                    "IPConfiguration prime began interface initialization",
                    strict_ipconfiguration_prime,
                ),
                Check(
                    "ipconfiguration_notifier_set_keys",
                    "PASS"
                    if has(r"PANTHERA:IPConfiguration notifier set keys returned ok=1", text)
                    else "FAIL",
                    "IPConfiguration registered dynamic-store notification keys",
                    strict_ipconfiguration_prime,
                ),
                Check(
                    "ipconfiguration_notifier_dispatch_queue",
                    "PASS"
                    if has(r"PANTHERA:IPConfiguration notifier dispatch queue returned ok=1", text)
                    else "FAIL",
                    "IPConfiguration scheduled dynamic-store notifications on its agent queue",
                    strict_ipconfiguration_prime,
                ),
                Check(
                    "ipconfiguration_prime_initialization_return",
                    "PASS"
                    if has(r"PANTHERA:IPConfiguration prime (start initialization|update interfaces) returned", text)
                    else "FAIL",
                    "IPConfiguration prime returned from interface initialization",
                    require_prime_unwind,
                ),
                Check(
                    "ipconfiguration_prime_server_init_enter",
                    "PASS" if has(r"PANTHERA:IPConfiguration prime server init enter", text) else "FAIL",
                    "IPConfiguration began control-server initialization",
                    require_prime_unwind,
                ),
                Check(
                    "ipconfiguration_prime_server_init_return",
                    "PASS" if has(r"PANTHERA:IPConfiguration prime server init returned", text) else "FAIL",
                    "IPConfiguration returned from control-server initialization",
                    require_prime_unwind,
                ),
                Check(
                    "ipconfiguration_prime_state_handler_enter",
                    "PASS" if has(r"PANTHERA:IPConfiguration prime state handler enter", text) else "FAIL",
                    "IPConfiguration began initial state handling",
                    require_prime_unwind,
                ),
                Check(
                    "ipconfiguration_prime_state_handler_return",
                    "PASS" if has(r"PANTHERA:IPConfiguration prime state handler returned", text) else "FAIL",
                    "IPConfiguration returned from initial state handling",
                    require_prime_unwind,
                ),
                Check(
                    "ipconfiguration_prime_handle_return",
                    "PASS" if has(r"PANTHERA:IPConfiguration handle prime returned", text) else "FAIL",
                    "IPConfiguration returned from handle_prime",
                    require_prime_unwind,
                ),
            ]
        )

    if strict_ipconfiguration_dhcp or strict_ipconfiguration_dhcp_packet or has(r"PANTHERA:DHCP thread enter if=en0 event=0", text):
        checks.extend(
            [
                Check(
                    "configd_setup_network_seed",
                    "PASS"
                    if has(r"PANTHERA:configd (setup preferences loaded services=[1-9][0-9]*|network-state (internal|lazy) published)", text)
                    else "FAIL",
                    "configd published Setup network preferences",
                    strict_ipconfiguration_dhcp,
                ),
                Check(
                    "ipconfiguration_setup_services",
                    "PASS" if has(r"PANTHERA:IPConfiguration entity all services=1", text) else "FAIL",
                    "IPConfiguration discovered one setup service",
                    strict_ipconfiguration_dhcp,
                ),
                Check(
                    "ipconfiguration_setup_ipv4_service",
                    "PASS" if has(r"PANTHERA:IPConfiguration entity append ipv4 service", text) else "FAIL",
                    "IPConfiguration found a setup IPv4 service",
                    strict_ipconfiguration_dhcp,
                ),
                Check(
                    "ipconfiguration_en0_service_match",
                    "PASS"
                    if has(r"PANTHERA:IPConfiguration interface config services if=en0 ipv4=1 count=1", text)
                    else "FAIL",
                    "IPConfiguration matched the DHCP service to en0",
                    strict_ipconfiguration_dhcp,
                ),
                Check(
                    "ipconfiguration_dhcp_service_add",
                    "PASS" if has(r"PANTHERA:IPConfiguration service add if=en0 method=DHCP", text) else "FAIL",
                    "IPConfiguration began adding the en0 DHCP service",
                    strict_ipconfiguration_dhcp,
                ),
                Check(
                    "ipconfiguration_dhcp_attach",
                    "PASS"
                    if has(r"PANTHERA:IPConfiguration service add ipv4 attach returned", text)
                    else "FAIL",
                    "IPConfiguration completed the IPv4 attach ioctl",
                    strict_ipconfiguration_dhcp,
                ),
                Check(
                    "ipconfiguration_dhcp_method_start",
                    "PASS"
                    if has(r"PANTHERA:IPConfiguration config method start if=en0 method=DHCP type=6", text)
                    else "FAIL",
                    "IPConfiguration entered DHCP method start for en0",
                    strict_ipconfiguration_dhcp,
                ),
                Check(
                    "ipconfiguration_dhcp_dispatch",
                    "PASS" if has(r"PANTHERA:IPConfiguration config method dispatch start", text) else "FAIL",
                    "IPConfiguration dispatched to the DHCP implementation",
                    strict_ipconfiguration_dhcp,
                ),
                Check(
                    "dhcp_thread_start",
                    "PASS" if has(r"PANTHERA:DHCP thread enter if=en0 event=0", text) else "FAIL",
                    "DHCP thread entered start event for en0",
                    strict_ipconfiguration_dhcp,
                ),
                Check(
                    "dhcp_timer_init",
                    "PASS" if has(r"PANTHERA:DHCP timer init returned ptr=0x[0-9a-fA-F]+", text) else "FAIL",
                    "DHCP timer callout initialized",
                    strict_ipconfiguration_dhcp,
                ),
                Check(
                    "dhcp_autoaddr_ioctl",
                    "PASS"
                    if has(r"PANTHERA:IPConfiguration service autoaddr enable ioctl returned ret=-?[0-9]+ errno=[0-9]+", text)
                    else "FAIL",
                    "IPConfiguration attempted the normal autoaddr ioctl path",
                    strict_ipconfiguration_dhcp,
                ),
                Check(
                    "dhcp_bootp_client_init",
                    "PASS" if has(r"PANTHERA:DHCP bootp client init returned ptr=0x[0-9a-fA-F]+", text) else "FAIL",
                    "DHCP initialized the BOOTP client",
                    strict_ipconfiguration_dhcp,
                ),
                Check(
                    "dhcp_arp_client_init",
                    "PASS" if has(r"PANTHERA:DHCP arp client init returned ptr=0x[0-9a-fA-F]+", text) else "FAIL",
                    "DHCP initialized the ARP client",
                    strict_ipconfiguration_dhcp,
                ),
                Check(
                    "dhcp_xid",
                    "PASS" if has(r"PANTHERA:DHCP xid (deterministic|random value=0x[0-9a-fA-F]+)", text) else "FAIL",
                    "DHCP assigned a transaction ID",
                    strict_ipconfiguration_dhcp,
                ),
                Check(
                    "dhcp_delayed_start_timer",
                    "PASS" if has(r"PANTHERA:DHCP delayed start timer set returned", text) else "FAIL",
                    "DHCP scheduled delayed start",
                    strict_ipconfiguration_dhcp,
                ),
                Check(
                    "ipconfiguration_dhcp_service_stored",
                    "PASS" if has(r"PANTHERA:IPConfiguration service add ipv4 stored", text) else "FAIL",
                    "IPConfiguration stored the en0 DHCP IPv4 service",
                    require_dhcp_unwind,
                ),
                Check(
                    "ipconfiguration_initial_config_return",
                    "PASS"
                    if has(r"PANTHERA:IPConfiguration start init configure cache returned", text)
                    else "FAIL",
                    "IPConfiguration completed its initial configuration/cache pass",
                    require_dhcp_unwind,
                ),
                Check(
                    "ipconfiguration_no_manual_initial_config",
                    "FAIL"
                    if has(r"PANTHERA:IPConfiguration prime initial configuration enter", text)
                    else "PASS",
                    "IPConfiguration no longer uses the Panthera prime-time configuration kick",
                    require_dhcp_unwind,
                ),
            ]
        )

    if strict_ipconfiguration_dhcp_packet or has(r"PANTHERA:DHCP delayed start fired", text):
        checks.extend(
            [
                Check(
                    "dhcp_delayed_start_fired",
                    "PASS" if has(r"PANTHERA:DHCP delayed start fired", text) else "FAIL",
                    "DHCP delayed-start timer fired",
                    strict_ipconfiguration_dhcp_packet,
                ),
                Check(
                    "dhcp_delayed_start_check_link",
                    "PASS" if has(r"PANTHERA:DHCP delayed start check-link enter", text) else "FAIL",
                    "DHCP delayed start entered link check",
                    strict_ipconfiguration_dhcp_packet,
                ),
                Check(
                    "dhcp_link_active",
                    "PASS" if has(r"PANTHERA:DHCP check link if=en0 event=0 valid=[01] active=[01] wait=0", text) else "FAIL",
                    "DHCP link check allowed INIT to proceed",
                    strict_ipconfiguration_dhcp_packet,
                ),
                Check(
                    "dhcp_init_enter",
                    "PASS" if has(r"PANTHERA:DHCP init start enter if=en0", text) else "FAIL",
                    "DHCP entered INIT for en0",
                    strict_ipconfiguration_dhcp_packet,
                ),
                Check(
                    "dhcp_init_request",
                    "PASS" if has(r"PANTHERA:DHCP init request made ptr=0x[0-9a-fA-F]+", text) else "FAIL",
                    "DHCP built the DISCOVER request",
                    strict_ipconfiguration_dhcp_packet,
                ),
                Check(
                    "dhcp_init_enable_receive",
                    "PASS" if has(r"PANTHERA:DHCP init enable receive returned", text) else "FAIL",
                    "DHCP enabled BOOTP receive callback",
                    strict_ipconfiguration_dhcp_packet,
                ),
                Check(
                    "dhcp_discover_transmit",
                    "PASS" if dhcp_packet_sent else "FAIL",
                    "DHCP DISCOVER transmit reached a successful send path",
                    strict_ipconfiguration_dhcp_packet,
                ),
                Check(
                    "dhcp_offer_receive",
                    "PASS" if has(r"PANTHERA:DHCP verify ok type=2 server=10\.0\.2\.2", text) else "FAIL",
                    "DHCP OFFER was received and validated",
                    strict_ipconfiguration_dhcp_packet,
                ),
                Check(
                    "dhcp_request_transmit",
                    "PASS" if has(r"PANTHERA:DHCP select transmit returned ok", text) else "FAIL",
                    "DHCP REQUEST transmit reached a successful send path",
                    strict_ipconfiguration_dhcp_packet,
                ),
                Check(
                    "dhcp_ack_receive",
                    "PASS" if has(r"PANTHERA:DHCP verify ok type=5 server=10\.0\.2\.2", text) else "FAIL",
                    "DHCP ACK was received and validated",
                    strict_ipconfiguration_dhcp_packet,
                ),
                Check(
                    "dhcp_bound_set_address",
                    "PASS" if has(r"PANTHERA:DHCP bound set address returned", text) else "FAIL",
                    "DHCP bound path returned from address assignment",
                    strict_ipconfiguration_dhcp_packet,
                ),
            ]
        )

    if strict_ipconfiguration_boot or strict_ipconfiguration_network:
        checks.extend(
            [
                Check(
                    "ipconfiguration_address_ioctl",
                    "PASS" if has(r"PANTHERA:IPConfiguration service set address ioctl returned", text) else "FAIL",
                    "IPConfiguration returned from the normal address assignment ioctl",
                    True,
                ),
                Check(
                    "ipconfiguration_publish_sync",
                    "PASS" if (
                        has(r"PANTHERA:IPConfiguration publish sync returned", text)
                        or has(r"PANTHERA:IPConfiguration publish async returned", text)
                    ) else "FAIL",
                    "IPConfiguration published DHCP state",
                    True,
                ),
                Check(
                    "ipconfiguration_publish_global_ipv4",
                    "PASS" if (
                        has(r"PANTHERA:IPConfiguration publish global IPv4 ok=1", text)
                        or has(r"PANTHERA:configd service-to-global Global IPv4 published", text)
                        or (strict_ipconfiguration_network and daemon_network_store_passed)
                    ) else "FAIL",
                    "State:/Network/Global/IPv4 was published or verified in the dynamic store",
                    True,
                ),
                Check(
                    "ipconfiguration_publish_global_dns",
                    "PASS" if (
                        has(r"PANTHERA:IPConfiguration publish global DNS ok=1", text)
                        or has(r"PANTHERA:configd service-to-global Global DNS published", text)
                        or (
                            strict_ipconfiguration_network
                            and daemon_network_store_passed
                            and daemon_dns_passed
                        )
                    ) else "FAIL",
                    "State:/Network/Global/DNS was published or verified by daemon network probes",
                    True,
                ),
                Check(
                    "ipconfiguration_default_route",
                    "PASS"
                    if has(r"PANTHERA:configd (route-manager|IPMonitor) default route write=[1-9][0-9]*", text)
                    else "FAIL",
                    "DHCP default route was installed",
                    True,
                ),
                Check(
                    "ipconfiguration_no_sigill_reap",
                    "FAIL"
                    if has(
                        r"PANTHERA:launchd reap label=com\.apple\.IPConfiguration "
                        r"exec=/usr/libexec/ipconfiguration .*signal=4",
                        text,
                    )
                    else "PASS",
                    "IPConfiguration stayed alive without SIGILL restart during IPConfiguration gate",
                    True,
                ),
                Check(
                    "ipconfiguration_no_crash_reap",
                    "FAIL"
                    if has(
                        r"PANTHERA:launchd reap label=com\.apple\.IPConfiguration "
                        r"exec=/usr/libexec/ipconfiguration .*signaled=1 signal=(?!0)([0-9]+)",
                        text,
                    )
                    else "PASS",
                    "IPConfiguration stayed alive without any signal reap during IPConfiguration gate",
                    True,
                ),
                Check(
                    "configd_no_sigill_reap",
                    "FAIL"
                    if has(
                        r"PANTHERA:launchd reap label=com\.apple\.configd "
                        r"exec=/usr/sbin/configd .*signal=4",
                        text,
                    )
                    else "PASS",
                    "configd stayed alive without SIGILL restart during configd-owned IPConfiguration gate",
                    True,
                ),
                Check(
                    "configd_no_crash_reap",
                    "FAIL"
                    if has(
                        r"PANTHERA:launchd reap label=com\.apple\.configd "
                        r"exec=/usr/sbin/configd .*signaled=1 signal=(?!0)([0-9]+)",
                        text,
                    )
                    else "PASS",
                    "configd stayed alive without any signal reap during configd-owned IPConfiguration gate",
                    True,
                ),
            ]
        )

    if strict_ipconfiguration_network:
        checks.extend(
            [
		command_check(
		    statuses,
		    "ipconfiguration_daemon_ifconfig",
                    "daemon-owned address is visible to network tools",
                    text=text,
                    output_pattern=r"PANTHERA_NETPROBE_STAGE:verify_addr_up:done",
                ),
                command_check(
                    statuses,
                    "ipconfiguration_daemon_route",
                    "daemon-owned default route is visible to network tools",
                    text=text,
                    output_pattern=r"Destination\s+Gateway\s+Flags[\s\S]*(default|0\.0\.0\.0)\s+10\.0\.2\.2",
                ),
                command_check(
                    statuses,
                    "ipconfiguration_daemon_sc_probe",
                    "daemon-owned dynamic-store IPv4 and DNS state is visible",
                    text=text,
                ),
                command_check(
                    statuses,
                    "ipconfiguration_daemon_gateway_ping",
                    "daemon-owned network can ping the QEMU gateway",
                    text=text,
                    output_pattern=r"bytes from|packets received|seq=",
                ),
                command_check(
                    statuses,
                    "ipconfiguration_daemon_dns_tcp",
                    "daemon-owned network can complete DNS-over-TCP",
                    text=text,
                    output_pattern=r"dnsprobe: resolved|dnsprobe: tcp connect ok",
                ),
                command_check(
                    statuses,
                    "ipconfiguration_control",
                    "IPConfiguration control MIG service handles if_count",
                    text=text,
                    output_pattern=r"PANTHERA_IPCONFIG_CONTROL:if_count attempt=[0-9]+ kr=0 count=[1-9][0-9]*",
                ),
            ]
        )

    if verify_text is not None:
        checks.append(
            Check(
                "verify_exports",
                "PASS" if has(r"PASS.*All checks passed", verify_text) else "FAIL",
                "libSystem export verification",
            )
        )

    checks = [
        check for check in checks
        if not (
            check.name in {
                "net_flags",
                "net_route",
                "net_arp",
                "net_proto",
                "net_addr_finalize",
                "net_static_arp",
                "net_eflags",
                "netstat_after_ping",
            }
            and check.result == "EXPECTED-FAIL"
            and check.detail == "status marker missing"
            and not strict_network
        )
    ]
    return checks


def write_summary(path: Path, log_path: Path, verify_path: Path | None, checks: list[Check]) -> None:
    failed = [check for check in checks if check.required and check.result != "PASS"]
    expected = [check for check in checks if check.result == "EXPECTED-FAIL"]
    overall = "PASS" if not failed else "FAIL"

    lines = [
        "# Panthera Boot Verification Summary",
        "",
        f"Result: {overall}",
        "",
        "## Artifacts",
        "",
        f"- Boot log: `{log_path}`",
    ]
    if verify_path is not None:
        lines.append(f"- Link check: `{verify_path}`")
    lines.extend(
        [
            "",
            "## Checks",
            "",
            "| Check | Result | Detail |",
            "|---|---|---|",
        ]
    )
    for check in checks:
        lines.append(f"| {check.name} | {check.result} | {check.detail} |")

    lines.extend(["", "## Expected Failures", ""])
    if expected:
        for check in expected:
            lines.append(f"- {check.name}: {check.detail}")
    else:
        lines.append("- None.")

    if failed:
        lines.extend(["", "## Blocking Failures", ""])
        for check in failed:
            lines.append(f"- {check.name}: {check.detail}")

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", required=True, type=Path)
    parser.add_argument("--summary", required=True, type=Path)
    parser.add_argument("--verify-exports-log", type=Path)
    parser.add_argument("--strict-dns", action="store_true")
    parser.add_argument("--strict-network", action="store_true")
    parser.add_argument("--strict-configd", action="store_true")
    parser.add_argument("--strict-ipconfiguration", action="store_true")
    parser.add_argument("--strict-ipconfiguration-prime", action="store_true")
    parser.add_argument("--strict-ipconfiguration-dhcp", action="store_true")
    parser.add_argument("--strict-ipconfiguration-dhcp-packet", action="store_true")
    parser.add_argument("--strict-ipconfiguration-boot", action="store_true")
    parser.add_argument("--strict-ipconfiguration-network", action="store_true")
    parser.add_argument("--strict-dispatch-timer", action="store_true")
    parser.add_argument("--strict-mdns", action="store_true")
    args = parser.parse_args()

    text = args.log.read_text(encoding="utf-8", errors="replace")
    verify_text = None
    if args.verify_exports_log is not None and args.verify_exports_log.exists():
        verify_text = args.verify_exports_log.read_text(encoding="utf-8", errors="replace")

    checks = build_checks(
        text,
        verify_text,
        args.strict_dns,
        args.strict_network,
        args.strict_configd,
        args.strict_ipconfiguration,
        args.strict_ipconfiguration_prime,
        args.strict_ipconfiguration_dhcp,
        args.strict_ipconfiguration_dhcp_packet,
        args.strict_ipconfiguration_boot,
        args.strict_ipconfiguration_network,
        args.strict_dispatch_timer,
        args.strict_mdns,
    )
    args.summary.parent.mkdir(parents=True, exist_ok=True)
    write_summary(args.summary, args.log, args.verify_exports_log, checks)

    failed = [check for check in checks if check.required and check.result != "PASS"]
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
