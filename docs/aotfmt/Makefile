# How to build aotfmt.pdf
# (you need to run make 4 times)
#
# make
# make bib
# make
# make

.POSIX:

aotfmt:
	mkdir -p build
	xelatex -output-directory=build -file-line-error -interaction=nonstopmode -synctex=15 aotfmt.tex

bib:
	biber build/aotfmt.bcf

clean:
	rm -r build

aspell:
	aspell check aotfmt.tex -l en --mode=tex --home-dir='.'

.PHONY: clean bib aotfmt aspell
