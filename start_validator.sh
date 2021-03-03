#!/bin/bash
rm -rf ./test-ledger
npm run build:program-c
export RUST_LOG=solana_runtime::system_instruction_processor=trace,solana_runtime::message_processor=debug,solana_bpf_loader=debug,solana_rbpf=debug
solana-test-validator --log
