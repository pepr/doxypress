ACLOCAL_AMFLAGS= -I autotools/m4 --install
SUBDIRS=. bin

.PHONY: deploy

deploy:
	$(MAKE) -C bin deploy

dist-test-project:
	$(MAKE) $(AM_MAKE_FLAGS) -C bin dist-test-project
