
.area vector (ABS)

	vector_unused:
		.dw	_do_reset
	vector_swi3:
		.dw	_do_swi3
	vector_swi2:
		.dw	_do_swi2
	vector_firq:
		.dw	_do_firq
	vector_irq:
		.dw	_do_irq
	vector_swi:
		.dw	_do_swi
	vector_nmi:
		.dw	_do_nmi
	vector_reset:
		.dw	_do_reset

