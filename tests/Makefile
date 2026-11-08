CC := cc
CFLAGS := -std=gnu11 -Wall -Wextra -Werror -O2 -I../payload
WEBFLAGS := -DTURBO_DIR=\"./.web-tmp\" -DTURBO_PORT=0
.PHONY: check clean
check: turbo_test feedback_test upgrade_test target_test settings_test web_host web_open_test inject_remove_test service_test
	./turbo_test
	./feedback_test
	./upgrade_test
	./target_test
	./settings_test
	python3 web_test.py
	node page_update_test.js
	./web_open_test
	./inject_remove_test
	./service_test 0
	./service_test 1
	./service_test 2
	./service_test 3
	./service_test 4
	./service_test 5
	./service_test 6
	./service_test 7
	./service_test 8
	./service_test 9
	./service_test 10
upgrade_test: upgrade_test.c ../payload/upgrade.c ../payload/upgrade.h
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined upgrade_test.c ../payload/upgrade.c -o $@
web_open_test: web_open_test.c ../payload/web.c ../payload/web.h ../payload/settings.c ../payload/version.h ../payload/web_page.h
	$(CC) $(CFLAGS) web_open_test.c ../payload/settings.c -o $@
feedback_test: feedback_test.c ../payload/stub.c ../payload/stub.h ../payload/hook.h
	$(CC) $(CFLAGS) -O1 -g -pthread -fsanitize=address,undefined feedback_test.c ../payload/stub.c -o $@
target_test: target_test.c ../payload/target.c ../payload/target.h
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined target_test.c ../payload/target.c -o $@
service_test: service_test.c ../payload/main.c ../payload/version.h ../payload/settings.c ../payload/settings.h ../payload/target.c ../payload/target.h ../payload/upgrade.c ../payload/upgrade.h
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -DTURBO_DIR=\"./.service-tmp\" service_test.c ../payload/settings.c ../payload/target.c ../payload/upgrade.c -o $@
inject_remove_test: inject_remove_test.c mock/ps5/kernel.h ../payload/inject.c ../payload/stub.c ../payload/stub.h ../payload/inject.h
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -D__PROSPERO__ -Imock inject_remove_test.c ../payload/inject.c ../payload/stub.c -o $@
turbo_test: turbo_test.c ../payload/stub.c ../payload/stub.h
	$(CC) $(CFLAGS) turbo_test.c ../payload/stub.c -o $@
../payload/web_page.h: ../payload/web/portal.html ../payload/version.h ../tools/gen_page.py
	python3 ../tools/gen_page.py $< $@
settings_test: settings_test.c ../payload/settings.c ../payload/web.c ../payload/version.h ../payload/web_page.h
	$(CC) $(CFLAGS) settings_test.c ../payload/settings.c ../payload/web.c -o $@
web_host: web_host.c ../payload/settings.c ../payload/web.c ../payload/version.h ../payload/web_page.h
	$(CC) $(CFLAGS) $(WEBFLAGS) web_host.c ../payload/settings.c ../payload/web.c -o $@
clean:
	rm -f turbo_test feedback_test upgrade_test target_test settings_test web_host web_open_test inject_remove_test service_test
