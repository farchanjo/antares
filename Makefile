# Makefile for antares
#
# Self-documenting: run `make` (or `make help`) to list the available targets.
# TODO (antares maintainer): wire the Build/Test targets below to your
# project's real build system (cargo, npm, go, gradle, ...). The Spec targets
# are ready to use and drive the speckit spec-driven workflow.

.DEFAULT_GOAL := help

.PHONY: help build test asan leak lint check validate verify analyze spec-status

help: ## Show this help
	@awk 'BEGIN {FS = ":.*##"; printf "Usage: make <target>\n\nTargets:\n"} /^[a-zA-Z0-9_-]+:.*##/ {printf "  \033[36m%-14s\033[0m %s\n", $$1, $$2}' $(MAKEFILE_LIST)

# ---------------------------------------------------------------------------
# Build / Test — placeholders: replace with antares's real toolchain.
# ---------------------------------------------------------------------------

build: ## Configure and build the dev preset
	cmake --preset dev
	cmake --build --preset dev

test: ## Run the dev test preset
	ctest --preset dev

asan: ## Build and run the suite under AddressSanitizer
	cmake --preset dev-asan
	cmake --build --preset dev-asan
	ctest --preset dev-asan --output-on-failure

leak: ## Run the suite under LeakSanitizer (Linux) or leaks(1) (macOS)
	@if [ "$$(uname -s)" = "Darwin" ]; then \
		cmake --preset dev >/dev/null && cmake --build --preset dev >/dev/null && \
		leaks --atExit -- ./build/dev/antares_io_conformance; \
	else \
		ASAN_OPTIONS=detect_leaks=1 cmake --build --preset dev-asan >/dev/null && \
		ASAN_OPTIONS=detect_leaks=1 ./build/dev-asan/antares_io_conformance; \
	fi

lint: ## Run the C++ rule checkers the validator registry cannot express
	@tools/check-include-paths.sh
	@tools/check-domain-purity.sh
	@tools/check-pins.sh

# ---------------------------------------------------------------------------
# Spec — speckit spec-driven workflow gates (ready to use).
# ---------------------------------------------------------------------------

validate: ## Validate the doc/arch spec corpus
	speckit validate

verify: ## Verify the executable specs against the implementation
	speckit verify

analyze: ## Analyze the spec corpus for gaps and drift
	speckit analyze

spec-status: ## Show the active feature and current workflow phase
	speckit status

check: validate verify analyze lint build test ## Run the full gate (corpus + rules + build)
