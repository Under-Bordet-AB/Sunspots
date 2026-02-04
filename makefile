.PHONY: all fetch_openmeteo fetch_elprisjustnu fetch_manager

all: fetch_manager fetch_openmeteo fetch_elprisjustnu

fetch_manager:
	$(MAKE) -C src/fetch -f makefile_fetch_manager

fetch_openmeteo:
	$(MAKE) -C src/fetch/apis -f makefile_fetch_openmeteo

fetch_elprisjustnu:
	$(MAKE) -C src/fetch/apis -f makefile_fetch_elprisjustnu

run:
	cd src/fetch && ./fetch_manager 1337

clean:
	rm -f src/fetch/fetch_manager src/fetch/apis/fetch_openmeteo src/fetch/apis/fetch_elprisjustnu