#!/usr/bin/env bash
# ==============================================================================
# HFT-Simulation-Engine: Full Project Verification & Integration Workflow
# ==============================================================================
# Runs the full verification pipeline in strict order:
#   1. Compiles and executes every C++ unit test suite.
#   2. Compiles the C++ IPC producer binary.
#   3. Compiles both C++ benchmark binaries and benchmark unit tests.
#   4. Executes all Python unit and integration test suites.
#   5. Runs an end-to-end C++ producer -> OS pipe -> Python consumer test.
#   6. Runs a headless Matplotlib trading dashboard smoke test with real producer.
#   7. Runs small benchmark smoke tests to validate throughput/latency calculation & CSV generation.
#
# Exits with a non-zero exit code if any required step fails.
# Prints a comprehensive pass/fail summary report upon completion.
# ==============================================================================

set -euo pipefail

# Ensure script runs from the repository root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

# Directories
mkdir -p bin build benchmark_results

# Styling helpers
BOLD='\033[1m'
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

# Track overall test results
PASSED_STEPS=()
FAILED_STEPS=()

log_header() {
    echo -e "\n${BOLD}${BLUE}==============================================================================${NC}"
    echo -e "${BOLD}${BLUE}   $1${NC}"
    echo -e "${BOLD}${BLUE}==============================================================================${NC}"
}

log_step() {
    echo -e "\n${BOLD}==>${NC} ${BOLD}$1${NC}"
}

record_pass() {
    PASSED_STEPS+=("$1")
    echo -e "${GREEN}✓ PASSED: $1${NC}"
}

record_fail() {
    FAILED_STEPS+=("$1")
    echo -e "${RED}✗ FAILED: $1${NC}"
    print_summary
    exit 1
}

# Trap unexpected errors
trap 'record_fail "Unexpected script failure at line $LINENO"' ERR

log_header "HFT-Simulation-Engine: Full Project Verification Workflow"
echo "Repository Root : ${REPO_ROOT}"
echo "C++ Compiler    : $(c++ --version | head -n 1)"
echo "Python Runtime  : $(python3 --version)"

# ------------------------------------------------------------------------------
# STEP 1: Compile and Run C++ Unit Test Suites
# ------------------------------------------------------------------------------
log_header "Step 1: Compiling and Running C++ Unit Test Suites"

# 1.1 Strategy Tests (Module 2)
log_step "Building & running Strategy tests..."
c++ -std=c++17 -Wall -Wextra -O2 \
    engine/MarketData.cpp engine/Strategy.cpp tests/test_strategy.cpp \
    -o bin/test_strategy
./bin/test_strategy
record_pass "C++ Module 2 Strategy Tests (bin/test_strategy)"

# 1.2 Order Tests (Module 3)
log_step "Building & running Order tests..."
c++ -std=c++17 -Wall -Wextra -O2 \
    tests/test_order.cpp \
    -o bin/test_order
./bin/test_order
record_pass "C++ Module 3 Order Tests (bin/test_order)"

# 1.3 Execution Engine Tests (Module 4)
log_step "Building & running Execution Engine tests..."
c++ -std=c++17 -Wall -Wextra -O2 \
    engine/OrderBook.cpp engine/ExecutionEngine.cpp tests/test_execution_engine.cpp \
    -o bin/test_execution_engine
./bin/test_execution_engine
record_pass "C++ Module 4 Execution Engine Tests (bin/test_execution_engine)"

# 1.4 Portfolio Tests (Module 5)
log_step "Building & running Portfolio tests..."
c++ -std=c++17 -Wall -Wextra -O2 \
    engine/Portfolio.cpp tests/test_portfolio.cpp \
    -o bin/test_portfolio
./bin/test_portfolio
record_pass "C++ Module 5 Portfolio Tests (bin/test_portfolio)"

# 1.5 Risk Manager Tests (Module 6)
log_step "Building & running Risk Manager tests..."
c++ -std=c++17 -Wall -Wextra -O2 \
    engine/Portfolio.cpp engine/RiskManager.cpp tests/test_risk_manager.cpp \
    -o bin/test_risk_manager
./bin/test_risk_manager
record_pass "C++ Module 6 Risk Manager Tests (bin/test_risk_manager)"

# 1.6 IPC Protocol Tests (Module 7)
log_step "Building & running IPC Protocol C++ tests..."
c++ -std=c++17 -Wall -Wextra -O2 \
    engine/Portfolio.cpp engine/IPCProtocol.cpp tests/test_ipc_protocol.cpp \
    -o bin/test_ipc_protocol
./bin/test_ipc_protocol
record_pass "C++ Module 7 IPC Protocol Tests (bin/test_ipc_protocol)"

# 1.7 Benchmark Utilities Tests (Module 9)
log_step "Building & running Benchmark Utilities tests..."
c++ -std=c++17 -Wall -Wextra -Werror -O3 \
    engine/MarketData.cpp engine/OrderBook.cpp engine/ExecutionEngine.cpp \
    tests/test_benchmark.cpp \
    -o bin/test_benchmark
./bin/test_benchmark
record_pass "C++ Module 9 Benchmark Tests (bin/test_benchmark)"

# ------------------------------------------------------------------------------
# STEP 2: Compile Standalone C++ Binaries
# ------------------------------------------------------------------------------
log_header "Step 2: Compiling Standalone Executables"

# 2.1 C++ IPC Producer
log_step "Compiling standalone IPC Producer binary..."
c++ -std=c++17 -Wall -Wextra -O2 \
    engine/MarketData.cpp engine/OrderBook.cpp \
    engine/ExecutionEngine.cpp engine/Portfolio.cpp \
    engine/RiskManager.cpp engine/IPCProtocol.cpp \
    engine/ipc_producer.cpp \
    -o bin/ipc_producer
record_pass "Compilation: C++ IPC Producer (bin/ipc_producer)"

# 2.2 Market Data Benchmark
log_step "Compiling Market Data Benchmark executable..."
c++ -std=c++17 -Wall -Wextra -Werror -O3 \
    engine/MarketData.cpp benchmarks/benchmark_market_data.cpp \
    -o bin/benchmark_market_data
record_pass "Compilation: Market Data Benchmark (bin/benchmark_market_data)"

# 2.3 Order Execution Benchmark
log_step "Compiling Order Execution Benchmark executable..."
c++ -std=c++17 -Wall -Wextra -Werror -O3 \
    engine/OrderBook.cpp engine/ExecutionEngine.cpp \
    benchmarks/benchmark_order_execution.cpp \
    -o bin/benchmark_order_execution
record_pass "Compilation: Order Execution Benchmark (bin/benchmark_order_execution)"

# ------------------------------------------------------------------------------
# STEP 3: Run All Python Unit and Integration Tests
# ------------------------------------------------------------------------------
log_header "Step 3: Running Python Unit & Integration Test Suites"

log_step "Running unittest discovery across tests/..."
python3 -m unittest discover -s tests -p "test_*.py" -v
record_pass "Python Test Discovery (test_ipc_consumer, test_dashboard, test_integration)"

# ------------------------------------------------------------------------------
# STEP 4: End-to-End Producer -> OS Pipe -> Python Consumer Test
# ------------------------------------------------------------------------------
log_header "Step 4: End-to-End Producer -> Pipe -> Consumer Verification"

log_step "Running C++ producer to Python IPC consumer demonstration..."
python3 visualizer/run_ipc_demo.py --ticks 15 --seed 42 --price 100.0 --cash 10000.0 --quiet
record_pass "End-to-End IPC Stream Demonstration (visualizer/run_ipc_demo.py)"

# ------------------------------------------------------------------------------
# STEP 5: Headless Live Visualizer Dashboard Smoke Test
# ------------------------------------------------------------------------------
log_header "Step 5: Headless Live Visualizer Dashboard Smoke Test"

SMOKE_DASH_IMG="build/dashboard_smoke_verification.png"
rm -f "${SMOKE_DASH_IMG}"

log_step "Rendering live visualizer dashboard with real C++ producer in headless mode..."
python3 visualizer/dashboard.py \
    --headless \
    --producer-bin bin/ipc_producer \
    --ticks 20 \
    --seed 42 \
    --price 100.0 \
    --cash 10000.0 \
    --interval 50 \
    --output "${SMOKE_DASH_IMG}"

if [ ! -s "${SMOKE_DASH_IMG}" ]; then
    echo -e "${RED}[ERROR] Headless dashboard failed to generate valid image file at ${SMOKE_DASH_IMG}${NC}"
    record_fail "Headless Visualizer Dashboard Smoke Test"
fi
# Clean up smoke output
rm -f "${SMOKE_DASH_IMG}"
record_pass "Headless Live Visualizer Dashboard Smoke Test (visualizer/dashboard.py)"

# ------------------------------------------------------------------------------
# STEP 6: Benchmark Smoke Tests (Isolated Small Workloads)
# ------------------------------------------------------------------------------
log_header "Step 6: Small Benchmark Smoke Tests & Output Hygiene"

MD_SMOKE_CSV="benchmark_results/market_data_smoke.csv"
OE_SMOKE_CSV="benchmark_results/order_execution_smoke.csv"
rm -f "${MD_SMOKE_CSV}" "${OE_SMOKE_CSV}"

log_step "Running small Market Data benchmark smoke test (1000 ticks, 500 latency samples)..."
./bin/benchmark_market_data \
    --ticks 1000 \
    --seed 42 \
    --latency-samples 500 \
    --batch-size 100 \
    --output "${MD_SMOKE_CSV}"

if [ ! -s "${MD_SMOKE_CSV}" ]; then
    echo -e "${RED}[ERROR] Market Data benchmark failed to generate CSV report${NC}"
    record_fail "Market Data Benchmark Smoke Test"
fi
record_pass "Market Data Benchmark Smoke Test (CSV output validated)"

log_step "Running small Order Execution benchmark smoke test (1000 ops, 500 latency samples)..."
./bin/benchmark_order_execution \
    --ops 1000 \
    --latency-samples 500 \
    --batch-size 100 \
    --output "${OE_SMOKE_CSV}"

if [ ! -s "${OE_SMOKE_CSV}" ]; then
    echo -e "${RED}[ERROR] Order Execution benchmark failed to generate CSV report${NC}"
    record_fail "Order Execution Benchmark Smoke Test"
fi
record_pass "Order Execution Benchmark Smoke Test (CSV output validated)"

# Clean up smoke CSV files to avoid committing artifacts
rm -f "${MD_SMOKE_CSV}" "${OE_SMOKE_CSV}"

# ------------------------------------------------------------------------------
# STEP 7: Final Pass/Fail Summary
# ------------------------------------------------------------------------------
print_summary() {
    log_header "Verification Workflow Summary"
    echo -e "${BOLD}Results Summary:${NC}"
    for step in "${PASSED_STEPS[@]}"; do
        echo -e "  ${GREEN}✓ [PASS]${NC} ${step}"
    done
    if [ ${#FAILED_STEPS[@]} -gt 0 ]; then
        for step in "${FAILED_STEPS[@]}"; do
            echo -e "  ${RED}✗ [FAIL]${NC} ${step}"
        done
        echo -e "\n${BOLD}${RED}OVERALL STATUS: FAILED (${#FAILED_STEPS[@]} failed steps)${NC}\n"
    else
        echo -e "\n${BOLD}${GREEN}OVERALL STATUS: ALL ${#PASSED_STEPS[@]} VERIFICATION STEPS PASSED SUCCESSFULLY!${NC}\n"
    fi
}

print_summary
exit 0
