.data
f:  .asciiz "Fizz\n"
b:  .asciiz "Buzz\n"
fb: .asciiz "FizzBuzz\n"
nl: .asciiz "\n"

.text
.globl main

main:
    addi $t0, $zero, 1

loop:
    slti $t1, $t0, 101
    beq $t1, $zero, exit

    addi $t1, $zero, 15
    div $t0, $t1
    mfhi $t2
    beq $t2, $zero, fizzbuzz

    addi $t1, $zero, 3
    div $t0, $t1
    mfhi $t2
    beq $t2, $zero, fizz

    addi $t1, $zero, 5
    div $t0, $t1
    mfhi $t2
    beq $t2, $zero, buzz

    add $a0, $t0, $zero
    addi $v0, $zero, 1
    syscall
    j newline

fizzbuzz:
    addi $v0, $zero, 4
    lui $a0, hi(fb)
    ori $a0, $a0, lo(fb)
    syscall
    j next

fizz:
    addi $v0, $zero, 4
    lui $a0, hi(f)
    ori $a0, $a0, lo(f)
    syscall
    j next

buzz:
    addi $v0, $zero, 4
    lui $a0, hi(b)
    ori $a0, $a0, lo(b)
    syscall
    j next

newline:
    addi $v0, $zero, 4
    lui $a0, hi(nl)
    ori $a0, $a0, lo(nl)
    syscall

next:
    addi $t0, $t0, 1
    j loop

exit:
    addi $v0, $zero, 10
    syscall