.PHONY: all test clean
all:
	$(MAKE) -C payload
test:
	$(MAKE) -C tests check
clean:
	$(MAKE) -C payload clean
	$(MAKE) -C tests clean
