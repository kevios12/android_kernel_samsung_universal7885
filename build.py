import platform
import distro
import time
import os
import shutil

out_path = ["out", "out2"]
menu_options = ["t", "b", "c", "e"]

folder_out = any(os.path.isdir(out) for out in out_path)

if folder_out:
    os.system("clear")
    print("\033[1m\033[33mOut folder detected. Source is not clean ...\033[0m")
    time.sleep(2)

def disk_space():
    stat = os.statvfs("/")
    total_space = stat.f_frsize * stat.f_blocks
    free_space = stat.f_frsize * stat.f_bavail
    total_gb = total_space / (2**30)
    free_gb = free_space / (2**30)
    print(f"Disk: {total_gb:.2f} GB / {free_gb:.2f} GB Free")

def glibc_check():
    if os.confstr("CS_GNU_LIBC_VERSION") == "glibc 2.39":
        return "\033[32m\u2713 Supported\033[0m"
    else:
        return "\033[1;31mX unsupported\033[0m"

def nameless_print():
    print(r" _   _                      _               ")
    print(r"| \ | | __ _ _ __ ___   ___| | ___  ___ __ __")
    print(r"|  \| |/ _` | '_ ` _ \ / _ \ |/ _ \/ __/ __||")
    print(r"| |\  | (_| | | | | | |  __/ |  __/\__ \__ \\")
    print(r"|_| \_|\__,_|_| |_| |_|\___|_|\___||___/___//")

def welcome():
    libc_version = os.confstr("CS_GNU_LIBC_VERSION")
    print("\033[1m\033[33m******* Nameless Kernel Builder Menu *******\033[0m")
    nameless_print()
    print(f"\nOS: {distro.name()} {distro.version()} with {libc_version} {glibc_check()}")
    print(f"Linux Kernel: {platform.release()}")
    disk_space()
    print("\nSelect an option:\n*****************")
    print("t = Toolchain")
    print("b = Build")
    print("c = Clean all")
    print("e = \033[1m\033[31mEXIT\n\033[0m*****************")

def clean_all():
    os.system("clear")
    for out in out_path:
        if os.path.isdir(out):
            print(f"\033[1m\033[31m Deleting {out}...\033[0m")
            shutil.rmtree(out)
            os.system("clear")
            print(f"\033[32m Deleted {out} \u2713\033[0m")
            time.sleep(2)
            os.system("clear")
        else:
            os.system("clear")
            print(f"\033[1m\033[31m Directory '{out}' does not exists. Skip!\033[0m")
            time.sleep(2)
    os.system("clear")
    print("Running 'make clean && make mrproper'...")
    os.system("make clean > /dev/null 2>&1 && make mrproper > /dev/null 2>&1")
    time.sleep(2)

while True:
    os.system("clear")
    welcome()

    select_input = input("Enter an option: ")

    if select_input in menu_options:
        if select_input == "a":
            print("nice!")
        elif select_input == "b":
            clean_all()
        elif select_input == "c":
            clean_all()
        elif select_input == "e":
            os.system("clear")
            print("\033[1m\033[31mExit!\033[0m Thank you for using Nameless Kernel Builder.")
            break
    else:
        print("\nSelected wrong Option!")
        time.sleep(2)

