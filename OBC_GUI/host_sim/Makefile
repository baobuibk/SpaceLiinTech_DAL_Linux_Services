.PHONY: run status check

run:
	$(MAKE) -C .. all
	../target/release/obc_gui host-sim

status:
	$(MAKE) -C .. all
	../target/release/obc_gui host-sim-status

check:
	$(MAKE) -C .. check
	$(MAKE) -C .. all
	python3 smoke_test.py ../target/release/obc_gui
