all: a4.pdf clean

a4.pdf: a4.tex single.pdf
	texfot pdflatex a4.tex

clean:
	rm -rf a4.aux a4.log

wipe:
	rm -rf a4.pdf

.PHONY: clean wipe
