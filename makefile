.PHONY: all fetch_openmeteo fetch_elprisjustnu fetch_manager

all: fetch_manager fetch_openmeteo fetch_elprisjustnu

fetch_manager:
	$(MAKE) -f makefile_fetch_manager

fetch_openmeteo:
	$(MAKE) -f makefile_fetch_openmeteo

fetch_elprisjustnu:
	$(MAKE) -f makefile_fetch_elprisjustnu

run:
	./fetch_manager 1337

clean:
	rm -f fetch_manager fetch_openmeteo fetch_elprisjustnu