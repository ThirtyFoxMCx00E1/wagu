// ==== _FINI_1 @ 00012658

void _FINI_1(void)

{
  (*(code *)PTR___cxa_finalize_0001cfc4)((int)&DAT_00012664 + DAT_00012664);
  return;
}


// ==== FUN_00012668 @ 00012668

void FUN_00012668(void)

{
  return;
}


// ==== _FINI_0 @ 0001266c

void _FINI_0(void)

{
  return;
}


// ==== FUN_000126ac @ 000126ac

void FUN_000126ac(undefined4 param_1)

{
  dlsym(*(undefined4 *)(DAT_000126b8 + 0x126b4),param_1);
  return;
}


// ==== _INIT_1 @ 000126bc

void _INIT_1(void)

{
  undefined4 uVar1;
  FILE *__stream;
  ulong uVar2;
  char *pcVar3;
  char *pcVar4;
  char acStack_81c [2048];
  int iStack_1c;
  
  iStack_1c = **(int **)(DAT_00012758 + 0x126d2);
  uVar1 = dlopen(DAT_00012754 + 0x126d0,1);
  pcVar4 = (char *)(DAT_00012760 + 0x126ea);
  pcVar3 = (char *)(DAT_00012764 + 0x126ec);
  *(undefined4 *)(DAT_0001275c + 0x126e8) = uVar1;
  __stream = fopen(pcVar4,pcVar3);
  if (__stream == (FILE *)0x0) {
    uVar2 = 0;
  }
  else {
    pcVar3 = (char *)(DAT_00012768 + 0x126fe);
    do {
      pcVar4 = fgets(acStack_81c,0x7ff,__stream);
      if (pcVar4 == (char *)0x0) {
        uVar2 = 0;
        goto LAB_0001272a;
      }
      pcVar4 = strstr(acStack_81c,pcVar3);
    } while (pcVar4 == (char *)0x0);
    uVar2 = strtoul(acStack_81c,(char **)0x0,0x10);
LAB_0001272a:
    fclose(__stream);
  }
  *(ulong *)(DAT_0001276c + 0x12736) = uVar2;
  if (**(int **)(DAT_00012770 + 0x12740) != iStack_1c) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_00012774 @ 00012774

void FUN_00012774(undefined4 param_1,undefined4 param_2,undefined4 param_3,undefined4 param_4)

{
  code *UNRECOVERED_JUMPTABLE;
  
  UNRECOVERED_JUMPTABLE = (code *)FUN_000126ac(DAT_000127a8 + 0x12788);
                    /* WARNING: Could not recover jumptable at 0x000127a6. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*UNRECOVERED_JUMPTABLE)(param_1,param_2,param_3,param_4);
  return;
}


// ==== FUN_000127ac @ 000127ac

void FUN_000127ac(undefined4 param_1,undefined4 param_2)

{
  code *UNRECOVERED_JUMPTABLE;
  
  UNRECOVERED_JUMPTABLE = (code *)FUN_000126ac(DAT_000127c8 + 0x127ba);
                    /* WARNING: Could not recover jumptable at 0x000127c6. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*UNRECOVERED_JUMPTABLE)(param_1,param_2);
  return;
}


// ==== FUN_000127cc @ 000127cc

void FUN_000127cc(undefined4 param_1,undefined4 param_2)

{
  code *UNRECOVERED_JUMPTABLE;
  
  UNRECOVERED_JUMPTABLE = (code *)FUN_000126ac(DAT_000127e8 + 0x127da);
                    /* WARNING: Could not recover jumptable at 0x000127e6. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*UNRECOVERED_JUMPTABLE)(param_1,param_2);
  return;
}


// ==== FUN_000127ec @ 000127ec

void FUN_000127ec(undefined4 param_1)

{
  code *UNRECOVERED_JUMPTABLE;
  
  UNRECOVERED_JUMPTABLE = (code *)FUN_000126ac(DAT_00012804 + 0x127f8);
                    /* WARNING: Could not recover jumptable at 0x00012802. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*UNRECOVERED_JUMPTABLE)(param_1);
  return;
}


// ==== FUN_00012808 @ 00012808

void FUN_00012808(undefined4 param_1)

{
  code *UNRECOVERED_JUMPTABLE;
  
  UNRECOVERED_JUMPTABLE = (code *)FUN_000126ac(DAT_00012820 + 0x12814);
                    /* WARNING: Could not recover jumptable at 0x0001281e. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*UNRECOVERED_JUMPTABLE)(param_1);
  return;
}


// ==== FUN_00012824 @ 00012824

void FUN_00012824(undefined4 param_1)

{
  code *UNRECOVERED_JUMPTABLE;
  
  UNRECOVERED_JUMPTABLE = (code *)FUN_000126ac(DAT_0001283c + 0x12830);
                    /* WARNING: Could not recover jumptable at 0x0001283a. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*UNRECOVERED_JUMPTABLE)(param_1);
  return;
}


// ==== FUN_00012840 @ 00012840

void FUN_00012840(undefined4 param_1,undefined4 param_2)

{
  code *UNRECOVERED_JUMPTABLE;
  
  UNRECOVERED_JUMPTABLE = (code *)FUN_000126ac(DAT_0001285c + 0x1284e);
                    /* WARNING: Could not recover jumptable at 0x0001285a. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*UNRECOVERED_JUMPTABLE)(param_1,param_2);
  return;
}


// ==== FUN_00012860 @ 00012860

void FUN_00012860(undefined4 param_1,undefined4 param_2)

{
  code *UNRECOVERED_JUMPTABLE;
  
  UNRECOVERED_JUMPTABLE = (code *)FUN_000126ac(DAT_0001287c + 0x1286e);
                    /* WARNING: Could not recover jumptable at 0x0001287a. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*UNRECOVERED_JUMPTABLE)(param_1,param_2);
  return;
}


// ==== FUN_00012880 @ 00012880

void FUN_00012880(undefined4 param_1,undefined4 param_2)

{
  code *UNRECOVERED_JUMPTABLE;
  
  UNRECOVERED_JUMPTABLE = (code *)FUN_000126ac(DAT_0001289c + 0x1288e);
                    /* WARNING: Could not recover jumptable at 0x0001289a. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*UNRECOVERED_JUMPTABLE)(param_1,param_2);
  return;
}


// ==== FUN_000128a0 @ 000128a0

void FUN_000128a0(undefined4 param_1,float param_2,float param_3,undefined4 param_4,
                 undefined4 param_5,undefined4 *param_6,undefined4 *param_7,undefined4 param_8,
                 undefined4 param_9,int param_10)

{
  code *pcVar1;
  undefined4 uVar2;
  uint in_fpscr;
  float fVar3;
  float fVar4;
  undefined4 local_44;
  undefined4 local_40;
  int local_3c;
  
  local_3c = **(int **)(DAT_000129a8 + 0x128c2);
  uVar2 = *(undefined4 *)(*(int *)(DAT_000129a4 + 0x128ba) + 8);
  fVar4 = (float)VectorSignedToFloat(*(undefined4 *)(*(int *)(DAT_000129a4 + 0x128ba) + 4),
                                     (byte)(in_fpscr >> 0x16) & 3);
  FUN_0001b32c(1);
  FUN_0001b348(1);
  local_40 = *param_6;
  FUN_0001b1d4(&local_40);
  FUN_0001b30c(param_8,param_9);
  FUN_0001b238(fVar4 * DAT_000129a0);
  FUN_0001b1b8(param_4);
  FUN_0001b21c(param_5);
  FUN_0001b2f0(1);
  FUN_0001b2b8(2);
  local_44 = *param_7;
  FUN_0001b270(&local_44);
  FUN_0001b2d4(1);
  pcVar1 = (code *)FUN_000126ac(DAT_000129ac + 0x12932);
  (*pcVar1)(param_1,DAT_000129b0 + 0x1293e);
  if (param_10 != 0) {
    pcVar1 = (code *)FUN_000126ac(DAT_000129b4 + 0x12950);
    (*pcVar1)(DAT_000129b8 + 0x1295a,DAT_000129b8 + 0x1295a);
  }
  fVar3 = (float)VectorSignedToFloat(uVar2,(byte)(in_fpscr >> 0x16) & 3);
  FUN_0001b3a8(param_2 * fVar4,param_3 * fVar3,DAT_000129bc + 0x1296e);
  FUN_0001b364();
  if (**(int **)(DAT_000129c0 + 0x12988) != local_3c) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== _INIT_2 @ 000129c4

void _INIT_2(void)

{
  int iVar1;
  int iVar2;
  undefined4 uVar3;
  int iVar4;
  int *piVar5;
  int *piVar6;
  
  piVar6 = (int *)(DAT_00012ab8 + 0x129da);
  iVar2 = DAT_00012ac0 + 0x129e0;
  *(int *)(DAT_00012abc + 0x129dc) = *piVar6 + 0x67947c;
  uVar3 = FUN_000126ac(iVar2);
  iVar2 = DAT_00012ac8 + 0x129f0;
  *(undefined4 *)(DAT_00012ac4 + 0x129ee) = uVar3;
  uVar3 = FUN_000126ac(iVar2);
  iVar2 = DAT_00012ad0 + 0x12a00;
  *(undefined4 *)(DAT_00012acc + 0x129fe) = uVar3;
  uVar3 = FUN_000126ac(iVar2);
  iVar2 = DAT_00012ad8 + 0x12a10;
  *(undefined4 *)(DAT_00012ad4 + 0x12a0e) = uVar3;
  uVar3 = FUN_000126ac(iVar2);
  iVar2 = DAT_00012ae0 + 0x12a20;
  *(undefined4 *)(DAT_00012adc + 0x12a1e) = uVar3;
  uVar3 = FUN_000126ac(iVar2);
  iVar2 = DAT_00012aec;
  piVar5 = (int *)(DAT_00012ae8 + 0x12a32);
  *(undefined4 *)(DAT_00012ae4 + 0x12a2e) = uVar3;
  *piVar5 = *piVar6 + 0x679a40;
  uVar3 = FUN_000126ac(iVar2 + 0x12a36);
  iVar2 = DAT_00012af4 + 0x12a50;
  *(undefined4 *)(DAT_00012af0 + 0x12a4e) = uVar3;
  uVar3 = FUN_000126ac(iVar2);
  iVar2 = DAT_00012afc + 0x12a60;
  *(undefined4 *)(DAT_00012af8 + 0x12a5e) = uVar3;
  uVar3 = FUN_000126ac(iVar2);
  iVar1 = DAT_00012b0c;
  iVar2 = DAT_00012b08;
  piVar5 = (int *)(DAT_00012b04 + 0x12a74);
  *(undefined4 *)(DAT_00012b00 + 0x12a6e) = uVar3;
  iVar4 = *piVar6;
  *piVar5 = iVar4 + 0x994ee8;
  *(int *)(iVar2 + 0x12a82) = iVar4 + 0x994eec;
  uVar3 = FUN_000126ac(iVar1 + 0x12a84);
  iVar2 = DAT_00012b14 + 0x12a9a;
  *(undefined4 *)(DAT_00012b10 + 0x12a98) = uVar3;
  uVar3 = FUN_000126ac(iVar2);
  iVar2 = DAT_00012b1c + 0x12aaa;
  *(undefined4 *)(DAT_00012b18 + 0x12aa8) = uVar3;
  uVar3 = FUN_000126ac(iVar2);
  *(undefined4 *)(DAT_00012b20 + 0x12ab6) = uVar3;
  return;
}


// ==== FUN_00012b24 @ 00012b24

undefined4 * FUN_00012b24(undefined4 *param_1,undefined4 param_2)

{
  undefined4 uVar1;
  code *pcVar2;
  undefined4 *puVar3;
  undefined4 *puVar4;
  int iVar5;
  
  uVar1 = dlopen(DAT_00012b68 + 0x12b34,0);
  pcVar2 = (code *)dlsym(uVar1,DAT_00012b6c + 0x12b3c);
  puVar3 = (undefined4 *)(*pcVar2)(DAT_00012b70 + 0x12b46);
  puVar4 = (undefined4 *)*puVar3;
  param_1[2] = puVar3;
  uVar1 = (*(code *)*puVar4)();
  param_1[1] = uVar1;
  *param_1 = param_2;
  iVar5 = *(int *)param_1[2];
  *(undefined2 *)(param_1 + 3) = 1;
  (**(code **)(iVar5 + 4))((int *)param_1[2],uVar1,param_2);
  return param_1;
}


// ==== FUN_00012b74 @ 00012b74

void FUN_00012b74(undefined4 *param_1)

{
  char cVar1;
  bool bVar2;
  
  cVar1 = *(char *)(param_1 + 3);
  bVar2 = cVar1 == '\x01';
  if (bVar2) {
    cVar1 = *(char *)((int)param_1 + 0xd);
  }
  if (bVar2 && cVar1 == '\x01') {
    *(undefined1 *)((int)param_1 + 0xd) = 0;
                    /* WARNING: Could not recover jumptable at 0x00012b9e. Too many branches */
                    /* WARNING: Treating indirect jump as call */
    (**(code **)(*(int *)param_1[2] + 8))((int *)param_1[2],param_1[1],*param_1);
    return;
  }
  return;
}


// ==== FUN_00012ba0 @ 00012ba0

uint * FUN_00012ba0(undefined4 *param_1,int param_2,char *param_3,int param_4)

{
  char cVar1;
  char *__s1;
  int iVar2;
  uint uVar3;
  undefined1 extraout_r1;
  undefined1 uVar4;
  uint *puVar5;
  bool bVar6;
  
  puVar5 = (uint *)0x0;
  if (*(char *)(param_1 + 3) == '\x01') {
    puVar5 = operator_new(0x390);
    *(undefined1 *)(puVar5 + 1) = 0;
    __aeabi_memclr4(puVar5 + 0x24,0x300);
    *puVar5 = (uint)param_1;
    iVar2 = 0;
    do {
      if (iVar2 == 0x3f) {
        *(undefined1 *)((int)puVar5 + 0x45) = 0;
        break;
      }
      cVar1 = *(char *)(param_4 + iVar2);
      *(char *)((int)puVar5 + iVar2 + 6) = cVar1;
      iVar2 = iVar2 + 1;
    } while (cVar1 != '\0');
    iVar2 = 0;
    do {
      if (iVar2 == 0x3f) {
        *(undefined1 *)((int)puVar5 + 0x85) = 0;
        break;
      }
      cVar1 = *(char *)(param_2 + iVar2);
      *(char *)((int)puVar5 + iVar2 + 0x46) = cVar1;
      iVar2 = iVar2 + 1;
    } while (cVar1 != '\0');
    iVar2 = -0x17f;
    do {
      if (iVar2 == 0) {
        *(undefined1 *)((int)puVar5 + 0x38f) = 0;
        break;
      }
      cVar1 = param_3[iVar2 + 0x17f];
      *(char *)((int)puVar5 + iVar2 + 0x38f) = cVar1;
      iVar2 = iVar2 + 1;
    } while (cVar1 != '\0');
    __s1 = (char *)(**(code **)(*(int *)param_1[2] + 0xc))
                             ((int *)param_1[2],param_1[1],param_4,param_2);
    if (*__s1 == '\0') {
      *(undefined1 *)((int)puVar5 + 5) = 0;
      FUN_00012cb8(puVar5,param_3);
    }
    else {
      cVar1 = *(char *)(*puVar5 + 0xd);
      FUN_00012cb8(puVar5,__s1);
      iVar2 = strcmp(__s1,param_3);
      uVar3 = (uint)(iVar2 != 0);
      *(bool *)((int)puVar5 + 5) = iVar2 != 0;
      bVar6 = cVar1 == '\0';
      uVar4 = extraout_r1;
      if (bVar6) {
        uVar3 = *puVar5;
        uVar4 = 0;
      }
      if (bVar6) {
        *(undefined1 *)(uVar3 + 0xd) = uVar4;
      }
    }
    cVar1 = *(char *)(param_1 + 3);
    bVar6 = cVar1 == '\x01';
    if (bVar6) {
      cVar1 = *(char *)((int)param_1 + 0xd);
    }
    if (bVar6 && cVar1 == '\x01') {
      *(undefined1 *)((int)param_1 + 0xd) = 0;
      (**(code **)(*(int *)param_1[2] + 8))((int *)param_1[2],param_1[1],*param_1);
    }
    *(uint **)(DAT_00012cb4 + 0x12c94) = puVar5;
  }
  return puVar5;
}


// ==== FUN_00012cb8 @ 00012cb8

void FUN_00012cb8(int *param_1,char *param_2)

{
  char cVar1;
  int iVar2;
  undefined4 extraout_r0;
  int *piVar3;
  undefined4 extraout_r1;
  undefined4 uVar4;
  char cVar5;
  char *pcVar6;
  int iVar7;
  char *pcVar8;
  int *__nptr;
  
  if ((char)param_1[1] == '\x01') {
    cVar1 = *param_2;
    cVar5 = (char)param_1[0x24];
    if ((cVar1 != '\0') && (cVar1 == cVar5)) {
      pcVar8 = (char *)((int)param_1 + 0x91);
      pcVar6 = param_2;
      do {
        pcVar6 = pcVar6 + 1;
        cVar1 = *pcVar6;
        cVar5 = *pcVar8;
        if (cVar1 == '\0') break;
        pcVar8 = pcVar8 + 1;
      } while (cVar1 == cVar5);
    }
    if (cVar5 == cVar1) {
      return;
    }
  }
  __nptr = param_1 + 0x24;
  iVar2 = 0;
  do {
    if (iVar2 == 0x17e) {
      *(undefined1 *)((int)param_1 + 0x20e) = 0;
      break;
    }
    cVar1 = param_2[iVar2];
    *(char *)((int)param_1 + iVar2 + 0x90) = cVar1;
    iVar2 = iVar2 + 1;
  } while (cVar1 != '\0');
  *(undefined1 *)((int)param_1 + 0x20f) = 0;
  iVar2 = atoi((char *)__nptr);
  param_1[0x23] = iVar2;
  atof((char *)__nptr);
  iVar2 = *param_1;
  uVar4 = *(undefined4 *)(iVar2 + 4);
  piVar3 = *(int **)(iVar2 + 8);
  iVar7 = *piVar3;
  *(undefined1 *)(iVar2 + 0xd) = 1;
  *(undefined1 *)(param_1 + 1) = 1;
  param_1[0x22] = (int)(float)(double)CONCAT44(extraout_r1,extraout_r0);
  (**(code **)(iVar7 + 0x10))(piVar3,uVar4,(int)param_1 + 6,(int)param_1 + 0x46,__nptr);
  return;
}


// ==== FUN_00012d54 @ 00012d54

void FUN_00012d54(undefined4 param_1,undefined4 param_2,undefined4 param_3,undefined4 param_4,
                 undefined1 param_5)

{
  int iVar1;
  undefined1 uStack00000000;
  
  iVar1 = **(int **)(DAT_00012d90 + 0x12d62);
  __vsnprintf_chk(param_1,param_3,0,param_2,param_4,&param_5,&param_5);
  if (**(int **)(DAT_00012d94 + 0x12d82) != iVar1) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_00012d98 @ 00012d98

char * FUN_00012d98(undefined4 *param_1,undefined4 param_2,char *param_3,undefined4 param_4)

{
  char cVar1;
  char *pcVar2;
  int iVar3;
  bool bVar4;
  
  if (*(char *)(param_1 + 3) == '\x01') {
    pcVar2 = (char *)(**(code **)(*(int *)param_1[2] + 0xc))
                               ((int *)param_1[2],param_1[1],param_4,param_2);
    if (*pcVar2 == '\0') {
      iVar3 = *(int *)param_1[2];
      *(undefined1 *)((int)param_1 + 0xd) = 1;
      (**(code **)(iVar3 + 0x10))((int *)param_1[2],param_1[1],param_4,param_2,param_3);
      cVar1 = *(char *)(param_1 + 3);
      bVar4 = cVar1 == '\x01';
      if (bVar4) {
        cVar1 = *(char *)((int)param_1 + 0xd);
      }
      if (bVar4 && cVar1 == '\x01') {
        *(undefined1 *)((int)param_1 + 0xd) = 0;
        (**(code **)(*(int *)param_1[2] + 8))((int *)param_1[2],param_1[1],*param_1);
      }
      pcVar2 = param_3;
    }
  }
  else {
    pcVar2 = (char *)0x0;
  }
  return pcVar2;
}


// ==== FUN_00012e04 @ 00012e04

void FUN_00012e04(undefined4 *param_1,int param_2,undefined4 param_3,int param_4)

{
  char cVar1;
  char *__nptr;
  int iVar2;
  bool bVar3;
  undefined1 auStack_3c8 [16];
  undefined4 *local_3b8;
  undefined1 local_3b4;
  char local_3b2 [138];
  undefined1 auStack_328 [384];
  undefined1 auStack_1a8 [388];
  int local_24;
  
  local_24 = **(int **)(DAT_00012f18 + 0x12e1a);
  if (*(char *)(param_1 + 3) == '\x01') {
    local_3b4 = 0;
    __aeabi_memclr8(auStack_328,0x300);
    iVar2 = 0;
    do {
      if (iVar2 == 0x3f) {
        local_3b2[0x3f] = 0;
        break;
      }
      cVar1 = *(char *)(param_4 + iVar2);
      local_3b2[iVar2] = cVar1;
      iVar2 = iVar2 + 1;
    } while (cVar1 != '\0');
    iVar2 = 0;
    do {
      if (iVar2 == 0x3f) {
        local_3b2[0x7f] = 0;
        break;
      }
      cVar1 = *(char *)(param_2 + iVar2);
      local_3b2[iVar2 + 0x40] = cVar1;
      iVar2 = iVar2 + 1;
    } while (cVar1 != '\0');
    local_3b8 = param_1;
    FUN_00012d54(auStack_1a8,0x180,0x180,DAT_00012f1c + 0x12e88,param_3);
    __nptr = (char *)(**(code **)(*(int *)param_1[2] + 0xc))
                               ((int *)param_1[2],param_1[1],param_4,param_2);
    if (*__nptr == '\0') {
      iVar2 = DAT_00012f20 + 0x12eb4;
      *(undefined1 *)((int)param_1 + 0xd) = 1;
      FUN_00012d54(auStack_3c8,0x10,0x10,iVar2,param_3);
      (**(code **)(*(int *)param_1[2] + 0x10))
                ((int *)param_1[2],param_1[1],param_4,param_2,auStack_3c8);
      cVar1 = *(char *)(param_1 + 3);
      bVar3 = cVar1 == '\x01';
      if (bVar3) {
        cVar1 = *(char *)((int)param_1 + 0xd);
      }
      if (bVar3 && cVar1 == '\x01') {
        *(undefined1 *)((int)param_1 + 0xd) = 0;
        (**(code **)(*(int *)param_1[2] + 8))((int *)param_1[2],param_1[1],*param_1);
      }
    }
    else {
      atoi(__nptr);
    }
  }
  if (**(int **)(DAT_00012f24 + 0x12eec) != local_24) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail(local_24);
  }
  return;
}


// ==== FUN_00012f28 @ 00012f28

void FUN_00012f28(undefined4 *param_1,int param_2,float param_3,int param_4)

{
  char cVar1;
  char *__nptr;
  int iVar2;
  bool bVar3;
  undefined1 auStack_3e0 [24];
  undefined4 *local_3c8;
  undefined1 local_3c4;
  char local_3c2 [138];
  undefined1 auStack_338 [384];
  undefined1 auStack_1b8 [388];
  int local_34;
  
  local_34 = **(int **)(DAT_00013060 + 0x12f40);
  if (*(char *)(param_1 + 3) == '\x01') {
    local_3c4 = 0;
    __aeabi_memclr8(auStack_338,0x300);
    iVar2 = 0;
    do {
      if (iVar2 == 0x3f) {
        local_3c2[0x3f] = 0;
        break;
      }
      cVar1 = *(char *)(param_4 + iVar2);
      local_3c2[iVar2] = cVar1;
      iVar2 = iVar2 + 1;
    } while (cVar1 != '\0');
    iVar2 = 0;
    do {
      if (iVar2 == 0x3f) {
        local_3c2[0x7f] = 0;
        break;
      }
      cVar1 = *(char *)(param_2 + iVar2);
      local_3c2[iVar2 + 0x40] = cVar1;
      iVar2 = iVar2 + 1;
    } while (cVar1 != '\0');
    local_3c8 = param_1;
    FUN_00012d54(auStack_1b8,0x180,0x180,DAT_00013064 + 0x12fb8,(double)param_3);
    __nptr = (char *)(**(code **)(*(int *)param_1[2] + 0xc))
                               ((int *)param_1[2],param_1[1],param_4,param_2);
    if (*__nptr == '\0') {
      iVar2 = DAT_00013068 + 0x12ff0;
      *(undefined1 *)((int)param_1 + 0xd) = 1;
      FUN_00012d54(auStack_3e0,0x18,0x18,iVar2,(double)param_3);
      (**(code **)(*(int *)param_1[2] + 0x10))
                ((int *)param_1[2],param_1[1],param_4,param_2,auStack_3e0);
      cVar1 = *(char *)(param_1 + 3);
      bVar3 = cVar1 == '\x01';
      if (bVar3) {
        cVar1 = *(char *)((int)param_1 + 0xd);
      }
      if (bVar3 && cVar1 == '\x01') {
        *(undefined1 *)((int)param_1 + 0xd) = 0;
        (**(code **)(*(int *)param_1[2] + 8))((int *)param_1[2],param_1[1],*param_1);
      }
    }
    else {
      atof(__nptr);
    }
  }
  if (**(int **)(DAT_0001306c + 0x13028) != local_34) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail(local_34);
  }
  return;
}


// ==== FUN_00013070 @ 00013070

void FUN_00013070(undefined4 *param_1,int param_2,int param_3,int param_4)

{
  char cVar1;
  char *__nptr;
  int iVar2;
  int iVar3;
  int iVar4;
  bool bVar5;
  char local_3ae [906];
  int local_24;
  
  local_24 = **(int **)(DAT_00013158 + 0x13084);
  if (*(char *)(param_1 + 3) == '\x01') {
    iVar2 = 0;
    do {
      if (iVar2 == 0x3f) {
        local_3ae[0x3f] = 0;
        break;
      }
      cVar1 = *(char *)(param_4 + iVar2);
      local_3ae[iVar2] = cVar1;
      iVar2 = iVar2 + 1;
    } while (cVar1 != '\0');
    iVar2 = 0;
    do {
      if (iVar2 == 0x3f) {
        local_3ae[0x7f] = 0;
        break;
      }
      cVar1 = *(char *)(param_2 + iVar2);
      local_3ae[iVar2 + 0x40] = cVar1;
      iVar2 = iVar2 + 1;
    } while (cVar1 != '\0');
    __nptr = (char *)(**(code **)(*(int *)param_1[2] + 0xc))
                               ((int *)param_1[2],param_1[1],param_4,param_2);
    iVar2 = DAT_00013160;
    if (*__nptr == '\0') {
      iVar4 = *(int *)param_1[2];
      iVar3 = DAT_0001315c + 0x13106;
      *(undefined1 *)((int)param_1 + 0xd) = 1;
      iVar2 = iVar2 + 0x13110;
      if (param_3 == 0) {
        iVar2 = iVar3;
      }
      (**(code **)(iVar4 + 0x10))((int *)param_1[2],param_1[1],param_4,param_2,iVar2);
      cVar1 = *(char *)(param_1 + 3);
      bVar5 = cVar1 == '\x01';
      if (bVar5) {
        cVar1 = *(char *)((int)param_1 + 0xd);
      }
      if (bVar5 && cVar1 == '\x01') {
        *(undefined1 *)((int)param_1 + 0xd) = 0;
        (**(code **)(*(int *)param_1[2] + 8))((int *)param_1[2],param_1[1],*param_1);
      }
    }
    else {
      atoi(__nptr);
    }
  }
  if (**(int **)(DAT_00013164 + 0x1312e) != local_24) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail(local_24);
  }
  return;
}


// ==== FUN_00013168 @ 00013168

void FUN_00013168(undefined4 *param_1,int param_2,uint param_3,int param_4)

{
  char cVar1;
  char *pcVar2;
  undefined4 *puVar3;
  undefined1 extraout_r1;
  undefined1 uVar4;
  int iVar5;
  bool bVar6;
  undefined4 *local_3b8;
  undefined1 local_3b4;
  char local_3b2 [138];
  undefined1 auStack_328 [384];
  undefined1 auStack_1a8 [388];
  int local_24;
  
  local_24 = **(int **)(DAT_00013288 + 0x1317e);
  if (*(char *)(param_1 + 3) == '\x01') {
    local_3b4 = 0;
    __aeabi_memclr8(auStack_328,0x300);
    iVar5 = 0;
    do {
      if (iVar5 == 0x3f) {
        local_3b2[0x3f] = 0;
        break;
      }
      cVar1 = *(char *)(param_4 + iVar5);
      local_3b2[iVar5] = cVar1;
      iVar5 = iVar5 + 1;
    } while (cVar1 != '\0');
    iVar5 = 0;
    do {
      if (iVar5 == 0x3f) {
        local_3b2[0x7f] = 0;
        break;
      }
      cVar1 = *(char *)(param_2 + iVar5);
      local_3b2[iVar5 + 0x40] = cVar1;
      iVar5 = iVar5 + 1;
    } while (cVar1 != '\0');
    local_3b8 = param_1;
    FUN_00012d54(auStack_1a8,0x180,0x180,DAT_0001328c + 0x131fa,param_3 & 0xff,param_3 >> 8 & 0xff,
                 param_3 >> 0x10 & 0xff,param_3 >> 0x18);
    pcVar2 = (char *)(**(code **)(*(int *)param_1[2] + 0xc))
                               ((int *)param_1[2],param_1[1],param_4,param_2);
    if (*pcVar2 == '\0') {
      FUN_00012cb8(&local_3b8,auStack_1a8);
    }
    else {
      cVar1 = *(char *)((int)local_3b8 + 0xd);
      puVar3 = (undefined4 *)FUN_00012cb8(&local_3b8,pcVar2);
      bVar6 = cVar1 == '\0';
      uVar4 = extraout_r1;
      if (bVar6) {
        uVar4 = 0;
        puVar3 = local_3b8;
      }
      if (bVar6) {
        *(undefined1 *)((int)puVar3 + 0xd) = uVar4;
      }
    }
    cVar1 = *(char *)(param_1 + 3);
    bVar6 = cVar1 == '\x01';
    if (bVar6) {
      cVar1 = *(char *)((int)param_1 + 0xd);
    }
    if (bVar6 && cVar1 == '\x01') {
      *(undefined1 *)((int)param_1 + 0xd) = 0;
      (**(code **)(*(int *)param_1[2] + 8))((int *)param_1[2],param_1[1],*param_1);
    }
    FUN_00013294(&local_3b8);
  }
  if (**(int **)(DAT_00013290 + 0x1325e) != local_24) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_00013294 @ 00013294

void FUN_00013294(int param_1)

{
  uint uVar1;
  uint uVar2;
  uint uVar3;
  int iVar4;
  uint extraout_r1;
  uint extraout_r2;
  uint extraout_r3;
  float local_34;
  float local_30;
  float local_2c;
  float local_28;
  uint local_24;
  uint local_20;
  uint local_1c;
  uint local_18;
  int local_14;
  
  local_14 = **(int **)(DAT_0001348c + 0x132ac);
  iVar4 = sscanf((char *)(param_1 + 0x90),(char *)(DAT_00013488 + 0x132aa),(int *)&local_18,
                 (int *)&local_1c,(int *)&local_20,(int *)&local_24);
  if (iVar4 == 3) {
    if (local_18 < 0x100) {
      uVar1 = local_1c;
      uVar2 = extraout_r2;
      if (local_1c < 0x100) {
        uVar1 = local_20;
        uVar2 = local_20;
      }
      if (uVar1 < 0x100) {
        local_18 = local_18 | local_1c << 8 | uVar2 << 0x10 | 0xff000000;
        *(uint *)(param_1 + 0x8c) = local_18;
        goto LAB_0001345a;
      }
    }
  }
  else if (iVar4 == 4) {
    uVar1 = local_18;
    uVar2 = extraout_r1;
    if (local_18 < 0x100) {
      uVar1 = local_1c;
      uVar2 = local_1c;
    }
    if (uVar1 < 0x100) {
      uVar1 = local_20;
      uVar3 = extraout_r3;
      if (local_20 < 0x100) {
        uVar1 = local_24;
        uVar3 = local_24;
      }
      if (uVar1 < 0x100) {
        local_18 = local_18 | uVar2 << 8 | local_20 << 0x10 | uVar3 << 0x18;
        *(uint *)(param_1 + 0x8c) = local_18;
        goto LAB_0001345a;
      }
    }
  }
  iVar4 = sscanf((char *)(param_1 + 0x90),(char *)(DAT_00013490 + 0x13316),(double *)&local_28,
                 (double *)&local_2c,(double *)&local_30,(double *)&local_34);
  if (iVar4 == 3) {
    if ((((0.0 <= local_28) && (local_28 <= 1.0)) && (0.0 <= local_2c)) &&
       (((local_2c <= 1.0 && (0.0 <= local_30)) && (local_30 <= 1.0)))) {
      iVar4 = ((uint)(0.0 < local_30 * DAT_00013484) * (int)(local_30 * DAT_00013484) * 0x10000 |
              (uint)(0.0 < local_2c * DAT_00013484) * (int)(local_2c * DAT_00013484) * 0x100) +
              (uint)(0.0 < local_28 * DAT_00013484) * (int)(local_28 * DAT_00013484) + -0x1000000;
      goto LAB_00013452;
    }
  }
  else if (((iVar4 == 4) && (0.0 <= local_28)) &&
          (((local_28 <= 1.0 &&
            ((((0.0 <= local_2c && (local_2c <= 1.0)) && (0.0 <= local_30)) &&
             ((local_30 <= 1.0 && (0.0 <= local_34)))))) && (local_34 <= 1.0)))) {
    iVar4 = ((uint)(0.0 < local_34 * DAT_00013484) * (int)(local_34 * DAT_00013484) * 0x1000000 |
             (uint)(0.0 < local_30 * DAT_00013484) * (int)(local_30 * DAT_00013484) * 0x10000 |
            (uint)(0.0 < local_2c * DAT_00013484) * (int)(local_2c * DAT_00013484) * 0x100) +
            (uint)(0.0 < local_28 * DAT_00013484) * (int)(local_28 * DAT_00013484);
LAB_00013452:
    *(int *)(param_1 + 0x8c) = iVar4;
  }
  local_18 = *(uint *)(param_1 + 0x8c);
LAB_0001345a:
  if (**(int **)(DAT_00013494 + 0x13462) != local_14) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail(local_18);
  }
  return;
}


// ==== stbsp_set_separators @ 00013498

void stbsp_set_separators(undefined1 param_1,undefined1 param_2)

{
  undefined1 *puVar1;
  
  puVar1 = (undefined1 *)(DAT_000134ac + 0x134a2);
  *(undefined1 *)(DAT_000134a8 + 0x134a0) = param_1;
  *puVar1 = param_2;
  return;
}


// ==== stbsp_vsprintfcb @ 000134b0

void stbsp_vsprintfcb(code *param_1,undefined4 param_2,uint *param_3,uint *param_4,double *param_5)

{
  uint *puVar1;
  byte bVar2;
  uint *puVar3;
  uint uVar4;
  byte *pbVar5;
  uint *puVar6;
  uint uVar7;
  uint *puVar8;
  double *pdVar9;
  int extraout_r1;
  uint uVar10;
  byte *pbVar11;
  uint uVar12;
  byte *pbVar13;
  byte *pbVar14;
  uint uVar15;
  byte *pbVar16;
  uint uVar17;
  int iVar18;
  int iVar19;
  byte *pbVar20;
  uint uVar21;
  undefined8 *puVar22;
  int iVar23;
  byte *pbVar24;
  uint *puVar25;
  int iVar26;
  uint *puVar27;
  undefined1 *puVar28;
  bool bVar29;
  double dVar30;
  double dVar31;
  undefined8 uVar32;
  double dVar33;
  undefined8 uVar34;
  longlong lVar35;
  undefined1 auStackY_400234 [4194196];
  double *local_274;
  byte *local_264;
  byte *local_260;
  uint local_258;
  byte *local_254;
  int local_24c;
  uint *local_244;
  byte *local_240;
  undefined4 local_23c;
  byte local_238;
  undefined4 local_237;
  undefined1 local_230 [2];
  undefined1 local_22e [4];
  undefined2 uStack_22a;
  uint auStack_228 [16];
  byte local_1e8;
  undefined1 local_1e7;
  uint local_1e6 [111];
  undefined1 local_29;
  uint local_28;
  
  local_24c = 0;
  local_28 = **(uint **)(DAT_000141a0 + 0x134d0);
  puVar8 = (uint *)(local_230 + 1);
  puVar3 = param_3;
  local_274 = param_5;
  do {
    if (((uint)param_4 & 3) != 0) goto LAB_0001352a;
LAB_00014da8:
    uVar10 = *param_4;
    uVar17 = ~uVar10 & 0x80808080;
    uVar7 = uVar10 & 0xff;
    if (((uVar10 ^ 0x25252525) + 0xfefefeff & uVar17) == 0) {
      pbVar14 = (byte *)((int)param_3 + (0x200 - (int)puVar3));
      do {
        if ((uVar17 & uVar10 + 0xfefefeff) != 0) goto LAB_00013530;
        if ((param_1 != (code *)0x0) && ((int)pbVar14 < 4)) break;
        puVar25 = puVar3 + 1;
        *puVar3 = uVar10;
        pbVar14 = pbVar14 + -4;
        param_4 = param_4 + 1;
        uVar10 = *param_4;
        uVar17 = ~uVar10 & 0x80808080;
        uVar7 = uVar10 & 0xff;
        puVar3 = puVar25;
      } while (((uVar10 ^ 0x25252525) + 0xfefefeff & uVar17) == 0);
    }
LAB_0001352c:
    if (uVar7 == 0x25) {
      local_258 = 0;
      do {
        puVar25 = (uint *)((int)param_4 + 1);
        bVar2 = *(byte *)puVar25;
        uVar7 = 2;
        switch(bVar2) {
        case 0x20:
          uVar7 = 4;
          break;
        default:
          goto switchD_0001357a_caseD_21;
        case 0x23:
          uVar7 = 8;
          break;
        case 0x24:
          if ((int)(local_258 << 0x17) < 0) {
            uVar7 = (local_258 & 0x800) + 0x800;
          }
          else {
            uVar7 = 0x100;
          }
          break;
        case 0x27:
          uVar7 = 0x40;
          break;
        case 0x2b:
          break;
        case 0x2d:
          uVar7 = 1;
          break;
        case 0x30:
          puVar25 = (uint *)((int)param_4 + 2);
          bVar2 = *(byte *)puVar25;
          local_258 = local_258 | 0x10;
          goto switchD_0001357a_caseD_21;
        case 0x5f:
          uVar7 = 0x400;
        }
        local_258 = local_258 | uVar7;
        param_4 = puVar25;
      } while( true );
    }
LAB_00013530:
    bVar2 = (byte)uVar7;
    if (uVar7 == 0) {
      if (param_1 == (code *)0x0) {
        *(byte *)puVar3 = 0;
      }
      else if (0 < (int)puVar3 - (int)param_3) {
        (*param_1)(param_3,param_2,(int)puVar3 - (int)param_3);
      }
LAB_00014f14:
      if (**(uint **)(DAT_00014f3c + 0x14f1c) != local_28) {
                    /* WARNING: Subroutine does not return */
        __stack_chk_fail(local_28);
      }
      return;
    }
    if (param_1 != (code *)0x0) {
      iVar18 = (int)puVar3 - (int)param_3;
      if (0x1fe < iVar18) {
        puVar3 = (uint *)(*param_1)(param_3,param_2,iVar18);
        local_24c = local_24c + iVar18;
        if (puVar3 == (uint *)0x0) goto LAB_00014f14;
        bVar2 = (byte)*param_4;
        param_3 = puVar3;
      }
    }
    *(byte *)puVar3 = bVar2;
    param_4 = (uint *)((int)param_4 + 1);
    puVar3 = (uint *)((int)puVar3 + 1);
  } while( true );
switchD_0001357a_caseD_21:
  uVar7 = (uint)bVar2;
  if (uVar7 == 0x2a) {
    puVar25 = (uint *)((int)puVar25 + 1);
    uVar7 = (uint)*(byte *)puVar25;
    pbVar14 = *(byte **)local_274;
    local_274 = (double *)((int)local_274 + 4);
  }
  else {
    pbVar14 = (byte *)0x0;
    uVar10 = uVar7;
    if (uVar7 - 0x30 < 10) {
      do {
        puVar25 = (uint *)((int)puVar25 + 1);
        uVar7 = (uint)*(byte *)puVar25;
        pbVar14 = (byte *)(uVar10 + (int)pbVar14 * 10 + -0x30);
        uVar10 = uVar7;
      } while (uVar7 - 0x30 < 10);
    }
  }
  if (uVar7 == 0x2e) {
    uVar7 = (uint)*(byte *)((int)puVar25 + 1);
    if (uVar7 == 0x2a) {
      uVar7 = (uint)*(byte *)((int)puVar25 + 2);
      local_260 = *(byte **)local_274;
      puVar25 = (uint *)((int)puVar25 + 2);
      local_274 = (double *)((int)local_274 + 4);
    }
    else if (uVar7 - 0x30 < 10) {
      local_260 = (byte *)0x0;
      uVar10 = uVar7;
      puVar6 = (uint *)((int)puVar25 + 2);
      do {
        puVar25 = puVar6;
        uVar7 = (uint)(byte)*puVar25;
        local_260 = (byte *)(uVar10 + (int)local_260 * 10 + -0x30);
        uVar10 = uVar7;
        puVar6 = (uint *)((int)puVar25 + 1);
      } while (uVar7 - 0x30 < 10);
    }
    else {
      local_260 = (byte *)0x0;
      puVar25 = (uint *)((int)puVar25 + 1);
    }
  }
  else {
    local_260 = (byte *)0xffffffff;
  }
  param_4 = puVar25;
  switch(uVar7) {
  case 0x49:
    param_4 = (uint *)((int)puVar25 + 1);
    uVar7 = (uint)*(byte *)param_4;
    if (uVar7 == 0x33) {
      if (*(byte *)((int)puVar25 + 2) == 0x32) {
        param_4 = (uint *)((int)puVar25 + 3);
      }
    }
    else {
      if (uVar7 != 0x36) goto LAB_00013722;
      if (*(byte *)((int)puVar25 + 2) == 0x34) {
        param_4 = (uint *)((int)puVar25 + 3);
        local_258 = local_258 | 0x20;
      }
    }
    break;
  case 0x68:
    param_4 = (uint *)((int)puVar25 + 1);
    local_258 = local_258 | 0x200;
    if (*(byte *)param_4 == 0x68) {
      param_4 = (uint *)((int)puVar25 + 2);
    }
    break;
  case 0x6a:
  case 0x74:
  case 0x7a:
    param_4 = (uint *)((int)puVar25 + 1);
    break;
  case 0x6c:
    uVar7 = (uint)*(byte *)((int)puVar25 + 1);
    if (uVar7 == 0x6c) {
      param_4 = (uint *)((int)puVar25 + 2);
      local_258 = local_258 | 0x20;
      break;
    }
LAB_00013722:
    param_4 = (uint *)((int)puVar25 + 1);
    goto LAB_00013738;
  }
  uVar7 = (uint)(byte)*param_4;
LAB_00013738:
  pbVar11 = (byte *)(DAT_00013a74 + 0x13748);
  puVar25 = (uint *)&local_29;
  if (0x37 < uVar7 - 0x41) {
switchD_00013754_caseD_43:
    local_23c = (byte *)0x1;
    local_29 = (char)uVar7;
    local_258 = 0;
    local_230 = (undefined1  [2])((ushort)local_230[1] << 8);
    pbVar24 = (byte *)0x0;
    local_238 = 0;
    local_260 = (byte *)0x0;
    local_264 = (byte *)0x0;
    pbVar14 = (byte *)0x0;
    local_240 = (byte *)0x0;
    goto LAB_000147c0;
  }
  switch(uVar7) {
  case 0x41:
  case 0x61:
    iVar18 = DAT_00013a84 + 0x1387c;
    if (uVar7 != 0x41) {
      iVar18 = DAT_00013a80 + 0x1387a;
    }
    puVar25 = (uint *)((int)local_274 + 7U & 0xfffffff8);
    uVar10 = puVar25[1];
    if ((int)uVar10 < 0) {
      local_258 = local_258 | 0x80;
    }
    if (local_260 == (byte *)0xffffffff) {
      local_260 = (byte *)0x6;
    }
    local_274 = (double *)(puVar25 + 2);
    uVar17 = *puVar25;
    if ((int)(local_258 * 0x1000000) < 0) {
      bVar2 = 0x2d;
LAB_0001438a:
      iVar23 = 1;
      local_230 = (undefined1  [2])((ushort)bVar2 << 8);
    }
    else {
      if ((int)(local_258 << 0x1d) < 0) {
        bVar2 = 0x20;
        goto LAB_0001438a;
      }
      if ((int)(local_258 << 0x1e) < 0) {
        bVar2 = 0x2b;
        goto LAB_0001438a;
      }
      iVar23 = 0;
    }
    uVar4 = (uVar10 & 0x7fffffff) >> 0x14;
    uVar10 = uVar10 & 0xfffff;
    if (uVar4 == 0) {
      local_254 = (byte *)0x0;
      if (uVar17 != 0 || uVar10 != 0) {
        local_254 = (byte *)0xfffffc02;
      }
    }
    else {
      uVar10 = uVar10 + 0x100000;
      local_254 = (byte *)(uVar4 - 0x3ff);
    }
    local_22e[iVar23] = 0x78;
    uVar4 = (int)local_260 * 4 - 0x20;
    uVar21 = 0x8000000 << ((int)local_260 * -4 + 0x20U & 0xff);
    if (-1 < (int)uVar4) {
      uVar21 = 0x8000000 >> (uVar4 & 0xff);
    }
    uVar12 = 0x8000000 >> (((uint)local_260 & 0x3f) << 2);
    uVar10 = uVar10 << 8 | uVar17 >> 0x18;
    if (-1 < (int)uVar4) {
      uVar12 = 0;
    }
    uVar15 = uVar21 + uVar17 * 0x100;
    uVar4 = uVar10 + uVar12 + CARRY4(uVar21,uVar17 * 0x100);
    if (0xe < (int)local_260) {
      uVar4 = uVar10;
    }
    local_1e8 = *(byte *)(iVar18 + (uVar4 >> 0x1c));
    local_230[iVar23 + 1] = 0x30;
    local_230[0] = (char)iVar23 + '\x02';
    if (0xe < (int)local_260) {
      uVar15 = uVar17 << 8;
    }
    if (local_260 == (byte *)0x0) {
      pbVar24 = (byte *)0x0;
      puVar25 = (uint *)&local_1e7;
      local_244 = (uint *)&local_1e7;
    }
    else {
      local_1e7 = *(undefined1 *)(DAT_0001467c + 0x14440);
      pbVar5 = local_260;
      if ((byte *)0xc < local_260) {
        pbVar5 = (byte *)0xd;
      }
      pbVar24 = local_260 + -(int)pbVar5;
      puVar6 = local_1e6;
      if (pbVar24 == (byte *)0x0 || (int)local_260 < (int)pbVar5) {
        pbVar24 = (byte *)0x0;
      }
      do {
        uVar10 = uVar4 & 0xfffffff;
        uVar4 = uVar4 << 4 | uVar15 >> 0x1c;
        pbVar5 = pbVar5 + -1;
        uVar15 = uVar15 << 4;
        puVar25 = (uint *)((int)puVar6 + 1);
        *(byte *)puVar6 = *(byte *)(iVar18 + (uVar10 >> 0x18));
        puVar6 = puVar25;
      } while (pbVar5 != (byte *)0x0);
      local_244 = local_1e6;
    }
    local_237._0_1_ = 0x70;
    if (uVar7 == 0x41) {
      local_237._0_1_ = 0x50;
    }
    if ((int)local_254 < 0) {
      local_237._1_1_ = 0x2d;
      local_254 = (byte *)-(int)local_254;
    }
    else {
      local_237._1_1_ = 0x2b;
    }
    if ((uint)local_254 >> 3 < 0x7d) {
      if (99 < local_254) {
        iVar18 = 5;
        goto LAB_000144c6;
      }
      if (9 < local_254) {
        iVar18 = 4;
        bVar29 = true;
        goto LAB_000144ca;
      }
      local_237._2_1_ = (byte)local_254 | 0x30;
      local_238 = 3;
    }
    else {
      iVar18 = 6;
LAB_000144c6:
      bVar29 = false;
LAB_000144ca:
      local_238 = (byte)iVar18;
      iVar23 = (int)local_254 / 10;
      local_230[iVar18 + -8] = (byte)local_254 + (char)((uint)local_254 / 10) * -10 | 0x30;
      local_230[iVar18 + -9] =
           (char)iVar23 +
           ((char)((ulonglong)((longlong)iVar23 * 0x66666667) >> 0x22) - (char)(iVar23 >> 0x1f)) *
           -10 + '0';
      if (!bVar29) {
        iVar23 = (int)local_254 / 100;
        local_230[iVar18 + -10] =
             (char)iVar23 +
             ((char)((ulonglong)((longlong)iVar23 * 0x66666667) >> 0x22) - (char)(iVar23 >> 0x1f)) *
             -10 + '0';
        if (999 < local_254) {
          iVar23 = (int)local_254 / 1000;
          local_230[iVar18 + -0xb] =
               (char)iVar23 +
               ((char)((ulonglong)((longlong)iVar23 * 0x66666667) >> 0x22) - (char)(iVar23 >> 0x1f))
               * -10 + '0';
        }
      }
    }
    local_260 = (byte *)0x0;
    local_23c = (byte *)((int)puVar25 - (int)&local_1e8);
    local_240 = (byte *)((int)puVar25 - (int)local_244);
    local_264 = (byte *)0x3000001;
    puVar25 = (uint *)&local_1e8;
    goto LAB_000147c0;
  case 0x42:
  case 0x62:
    local_230 = (undefined1  [2])((ushort)local_230[1] << 8);
    local_254 = (byte *)(DAT_00013a8c + 0x138d4);
    if (uVar7 != 0x42) {
      local_254 = (byte *)(DAT_00013a88 + 0x138ce);
    }
    if ((int)(local_258 << 0x1c) < 0) {
      if (uVar7 != 0x42) {
        uVar7 = 0x62;
      }
      local_22e[0] = (undefined1)uVar7;
      local_230 = (undefined1  [2])0x3002;
    }
    local_23c = (byte *)0x180;
    break;
  default:
    goto switchD_00013754_caseD_43;
  case 0x45:
  case 0x65:
    if (local_260 == (byte *)0xffffffff) {
      local_260 = (byte *)0x6;
    }
    puVar22 = (undefined8 *)((int)local_274 + 7U & 0xfffffff8);
    iVar18 = FUN_00014f40((int)*puVar22,&local_244,&local_23c,auStack_228,&local_240,
                          (uint)local_260 | 0x80000000);
    if (iVar18 != 0) {
      local_258 = local_258 | 0x80;
    }
    local_274 = (double *)(puVar22 + 1);
    iVar18 = DAT_00013a7c + 0x1383e;
    if (uVar7 == 0x45) {
      iVar18 = DAT_00013a78 + 0x13838;
    }
LAB_00013846:
    local_230 = (undefined1  [2])((ushort)local_230 & 0xff00);
    if ((int)(local_258 << 0x18) < 0) {
      local_230[1] = 0x2d;
LAB_00013ede:
      local_230[0] = 1;
    }
    else {
      if ((int)(local_258 << 0x1d) < 0) {
        local_230[1] = 0x20;
        goto LAB_00013ede;
      }
      if ((int)(local_258 << 0x1e) < 0) {
        local_230[1] = 0x2b;
        goto LAB_00013ede;
      }
    }
    puVar25 = local_244;
    if (local_240 == (byte *)0x7000) goto LAB_00014038;
    local_1e8 = (byte)*local_244;
    puVar6 = (uint *)&local_1e7;
    if (local_260 != (byte *)0x0) {
      local_1e7 = *(undefined1 *)(DAT_000141b8 + 0x13f0c);
      puVar6 = local_1e6;
    }
    pbVar24 = local_23c + -1;
    if (local_260 < pbVar24) {
      local_23c = local_260 + 1;
    }
    puVar25 = puVar6;
    if ((byte *)0x1 < local_23c) {
      pbVar5 = (byte *)0x1;
      if (local_260 < pbVar24) {
        pbVar24 = local_260;
      }
      if (((byte *)0xf < pbVar24) && ((uint)((int)local_244 - (int)puVar6) < 0xfffffff0)) {
        pbVar16 = (byte *)((uint)pbVar24 & 0xfffffff0);
        pbVar20 = (byte *)((int)local_244 + 1);
        puVar25 = (uint *)((int)puVar6 + (int)pbVar16);
        pbVar5 = pbVar16 + 1;
        puVar27 = puVar6;
        pbVar13 = pbVar16;
        do {
          uVar32 = *(undefined8 *)pbVar20;
          uVar34 = *(undefined8 *)(pbVar20 + 8);
          pbVar20 = pbVar20 + 0x10;
          pbVar13 = pbVar13 + -0x10;
          *(undefined8 *)puVar27 = uVar32;
          *(undefined8 *)(puVar27 + 2) = uVar34;
          puVar27 = puVar27 + 4;
        } while (pbVar13 != (byte *)0x0);
        if (pbVar24 == pbVar16) goto LAB_00014662;
      }
      pbVar24 = (byte *)((int)local_244 + (int)pbVar5);
      puVar27 = puVar25;
      do {
        bVar29 = puVar27 != (uint *)((int)puVar6 + (int)(local_23c + -2));
        *(byte *)puVar27 = *pbVar24;
        pbVar24 = pbVar24 + 1;
        puVar25 = (uint *)((int)puVar27 + 1);
        puVar27 = (uint *)((int)puVar27 + 1);
      } while (bVar29);
    }
LAB_00014662:
    local_237._0_1_ = *(undefined1 *)(iVar18 + 0xe);
    if ((int)local_240 < 1) {
      local_237._1_1_ = 0x2d;
      pbVar24 = (byte *)(1 - (int)local_240);
    }
    else {
      pbVar24 = local_240 + -1;
      local_237._1_1_ = 0x2b;
    }
    iVar18 = 4;
    if ((byte *)0x63 < pbVar24) {
      iVar18 = 5;
    }
    local_238 = (byte)iVar18;
    local_240 = (byte *)((int)pbVar24 / 10);
    local_230[iVar18 + -8] = (char)pbVar24 + (char)((uint)pbVar24 / 10) * -10 | 0x30;
    local_230[iVar18 + -9] =
         (char)local_240 +
         ((char)((ulonglong)((longlong)(int)local_240 * 0x66666667) >> 0x22) -
         (char)((int)local_240 >> 0x1f)) * -10 + '0';
    if ((byte *)0x63 < pbVar24) {
      local_240 = (byte *)((int)pbVar24 / 100);
      local_230[iVar18 + -10] =
           (char)local_240 +
           ((char)((ulonglong)((longlong)(int)local_240 * 0x66666667) >> 0x22) -
           (char)((int)local_240 >> 0x1f)) * -10 + '0';
    }
    pbVar24 = local_260 + (1 - (int)local_23c);
    local_264 = (byte *)0x3000001;
    goto LAB_00014746;
  case 0x47:
  case 0x67:
    pbVar24 = (byte *)0x1;
    if ((byte *)0x1 < local_260) {
      pbVar24 = local_260;
    }
    if (local_260 == (byte *)0xffffffff) {
      pbVar24 = (byte *)0x6;
    }
    puVar22 = (undefined8 *)((int)local_274 + 7U & 0xfffffff8);
    iVar18 = FUN_00014f40((int)*puVar22,&local_244,&local_23c,auStack_228,&local_240,
                          (uint)(pbVar24 + -1) | 0x80000000);
    if (iVar18 != 0) {
      local_258 = local_258 | 0x80;
    }
    iVar18 = DAT_00013a94 + 0x13944;
    if (uVar7 == 0x47) {
      iVar18 = DAT_00013a90 + 0x13946;
    }
    if (pbVar24 <= local_23c) {
      local_23c = pbVar24;
    }
    if (local_23c < (byte *)0x2) {
      pbVar5 = pbVar24;
    }
    else {
      pbVar13 = pbVar24;
      while (*(byte *)((int)local_244 + (int)(local_23c + -1)) == 0x30) {
        local_23c = local_23c + -1;
        pbVar5 = pbVar13 + -1;
        if ((local_23c < (byte *)0x2) || (bVar29 = pbVar13 == (byte *)0x1, pbVar13 = pbVar5, bVar29)
           ) goto LAB_00013f94;
      }
      pbVar5 = pbVar13;
    }
LAB_00013f94:
    local_274 = (double *)(puVar22 + 1);
    if (((int)local_240 < -3) || ((int)pbVar24 < (int)local_240)) {
      if ((int)local_23c < (int)pbVar5) {
        local_260 = local_23c + -1;
      }
      else {
        local_260 = pbVar5 + -1;
        if (pbVar5 == (byte *)0x0) {
          local_260 = (byte *)0x0;
        }
      }
      goto LAB_00013846;
    }
    if ((int)local_240 < 1) {
      local_260 = local_23c;
      if ((int)pbVar5 < (int)local_23c) {
        local_260 = pbVar5;
      }
      local_260 = local_260 + -(int)local_240;
    }
    else {
      local_260 = local_23c + -(int)local_240;
      if (local_260 == (byte *)0x0 || (int)local_23c < (int)local_240) {
        local_260 = (byte *)0x0;
      }
    }
    goto LAB_00013ff0;
  case 99:
    local_23c = (byte *)0x1;
    local_29 = (char)*(uint *)local_274;
    local_230 = (undefined1  [2])((ushort)local_230[1] << 8);
    local_240 = (byte *)0x0;
    local_274 = (double *)((int)local_274 + 4);
    goto LAB_00014038;
  case 100:
  case 0x69:
  case 0x75:
    if ((int)(local_258 << 0x1a) < 0) {
      puVar25 = (uint *)((int)local_274 + 7U & 0xfffffff8);
      local_254 = (byte *)puVar25[1];
      local_274 = (double *)(puVar25 + 2);
      uVar10 = *puVar25;
      if ((uVar7 != 0x75) && (0x7fffffff < local_254)) {
        bVar29 = uVar10 != 0;
        uVar10 = -uVar10;
        local_258 = local_258 | 0x80;
        local_254 = (byte *)(-(uint)bVar29 - (int)local_254);
      }
    }
    else {
      pdVar9 = (double *)((int)local_274 + 4);
      uVar10 = *(uint *)local_274;
      local_254 = (byte *)0x0;
      local_274 = pdVar9;
      if (uVar7 != 0x75) {
        if (0x7fffffff < uVar10) {
          uVar10 = -uVar10;
          local_258 = local_258 | 0x80;
        }
        local_254 = (byte *)0x0;
      }
    }
    lVar35 = CONCAT44(local_254,uVar10);
    if (-1 < (int)(local_258 << 0x17)) {
      bVar2 = *pbVar11;
      iVar18 = 0x200;
      iVar23 = 0;
      do {
        local_254 = (byte *)((ulonglong)lVar35 >> 0x20);
        uVar7 = (uint)lVar35;
        if ((uint)-(int)local_254 < (uint)(uVar7 < 100000000)) {
          lVar35 = 0;
        }
        else {
          lVar35 = FUN_0001b728(uVar7,local_254,100000000,0);
          uVar7 = uVar7 + (int)lVar35 * -100000000;
        }
        puVar28 = local_230 + iVar18;
        iVar26 = local_258 * 0x2000000;
        if (iVar26 < 0) {
          for (; iVar19 = iVar18, uVar7 != 0; uVar7 = uVar7 / 10) {
            while (iVar26 < 0) {
              if (iVar23 != 3) {
                iVar23 = iVar23 + 1;
                break;
              }
              iVar18 = iVar19 + -1;
              puVar28 = puVar28 + -1;
              iVar23 = 0;
              *(byte *)((int)auStack_228 + iVar19 + -1) = bVar2;
              iVar19 = iVar18;
              if (uVar7 == 0) goto LAB_00013ade;
            }
            iVar18 = iVar19 + -1;
            *(byte *)((int)auStack_228 + iVar19 + -1) =
                 (char)uVar7 + (char)(uVar7 / 10) * -10 | 0x30;
          }
        }
        else {
          do {
            bVar29 = 99 < uVar7;
            *(undefined2 *)(local_22e + iVar18 + 4) =
                 *(undefined2 *)(DAT_00013a98 + (uVar7 % 100) * 2 + 0x13a44);
            iVar18 = iVar18 + -2;
            uVar7 = uVar7 / 100;
          } while (bVar29);
        }
LAB_00013ade:
        if (lVar35 == 0) goto LAB_00013b3e;
        while ((undefined1 *)((int)auStack_228 + iVar18) != puVar28) {
          if (iVar26 < 0) {
            if (iVar23 != 3) {
              iVar23 = iVar23 + 1;
              goto LAB_00013aec;
            }
            puVar28 = puVar28 + -1;
            iVar23 = 0;
            *(byte *)((int)auStack_228 + iVar18 + -1) = bVar2;
            iVar18 = iVar18 + -1;
          }
          else {
LAB_00013aec:
            *(undefined1 *)((int)auStack_228 + iVar18 + -1) = 0x30;
            iVar18 = iVar18 + -1;
          }
        }
      } while( true );
    }
    if (local_260 == (byte *)0xffffffff) {
      local_260 = (byte *)0x1;
    }
    dVar30 = (double)FUN_0001b4e8(uVar10,local_254);
    if (local_254 == (byte *)0x0 && (uint)(0x3ff < uVar10) <= local_254) {
      local_260 = (byte *)0x0;
    }
    uVar7 = local_258;
LAB_00013de2:
    pdVar9 = (double *)&DAT_000141a8;
    if (-1 < (int)(uVar7 << 0x14)) {
      pdVar9 = (double *)&DAT_000141b0;
    }
    dVar31 = *pdVar9;
    dVar33 = -dVar31;
    if ((dVar31 <= dVar30) || (local_258 = uVar7, dVar30 <= dVar33)) {
      dVar30 = dVar30 / dVar31;
      local_258 = uVar7 + 0x1000000;
      if ((dVar31 <= dVar30) || (dVar30 <= dVar33)) {
        dVar30 = dVar30 / dVar31;
        local_258 = uVar7 + 0x2000000;
        if ((dVar31 <= dVar30) || (dVar30 <= dVar33)) {
          dVar30 = dVar30 / dVar31;
          local_258 = uVar7 + 0x3000000;
          if ((dVar31 <= dVar30) || (dVar30 <= dVar33)) {
            dVar30 = dVar30 / dVar31;
            local_258 = uVar7 | 0x4000000;
          }
        }
      }
    }
    goto LAB_00013e80;
  case 0x66:
    pdVar9 = (double *)((int)local_274 + 7U & 0xfffffff8);
    local_274 = pdVar9 + 1;
    dVar30 = *pdVar9;
    uVar7 = local_258;
    if ((int)(local_258 << 0x17) < 0) goto LAB_00013de2;
LAB_00013e80:
    if (local_260 == (byte *)0xffffffff) {
      local_260 = (byte *)0x6;
    }
    iVar18 = FUN_00014f40(SUB84(dVar30,0),&local_244,&local_23c,auStack_228,&local_240,local_260);
    if (iVar18 != 0) {
      local_258 = local_258 | 0x80;
    }
LAB_00013ff0:
    pbVar24 = local_240;
    local_230 = (undefined1  [2])((ushort)local_230 & 0xff00);
    local_238 = 0;
    if ((int)(local_258 << 0x18) < 0) {
      local_230[1] = 0x2d;
LAB_00014020:
      local_230[0] = 1;
    }
    else {
      if ((int)(local_258 << 0x1d) < 0) {
        local_230[1] = 0x20;
        goto LAB_00014020;
      }
      if ((int)(local_258 << 0x1e) < 0) {
        local_230[1] = 0x2b;
        goto LAB_00014020;
      }
    }
    puVar25 = local_244;
    if (local_240 == (byte *)0x7000) {
LAB_00014038:
      local_238 = 0;
      local_264 = (byte *)0x0;
      pbVar24 = (byte *)0x0;
      local_260 = (byte *)0x0;
    }
    else {
      if ((int)local_240 < 1) {
        local_1e8 = 0x30;
        puVar25 = (uint *)&local_1e7;
        if (local_260 != (byte *)0x0) {
          local_1e7 = *(undefined1 *)(DAT_000141bc + 0x14066);
          puVar25 = local_1e6;
        }
        pbVar24 = (byte *)-(int)local_240;
        if ((int)local_260 < -(int)local_240) {
          pbVar24 = local_260;
        }
        if ((pbVar24 == (byte *)0x0) || (((uint)puVar25 & 3) == 0)) {
          pbVar5 = pbVar24;
        }
        else {
          pbVar13 = pbVar24 + -1;
          pbVar5 = (byte *)(~(uint)puVar25 & 3);
          if (pbVar13 <= (byte *)(~(uint)puVar25 & 3)) {
            pbVar5 = pbVar13;
          }
          __aeabi_memset(puVar25,pbVar5 + 1,0x30);
          puVar25 = (uint *)((int)puVar25 + (int)(pbVar5 + 1));
          pbVar5 = pbVar13 + -(int)pbVar5;
        }
        if (3 < (int)pbVar5) {
          pbVar13 = pbVar5 + -7;
          if (pbVar5 < (byte *)0x7) {
            pbVar13 = (byte *)0x0;
          }
          iVar18 = ((uint)(pbVar13 + 3) & 0xfffffffc) + 4;
          __aeabi_memset4(puVar25,iVar18,0x30);
          puVar25 = (uint *)((int)puVar25 + iVar18);
          pbVar5 = pbVar5 + (-4 - ((uint)(pbVar13 + 3) & 0xfffffffc));
        }
        if (pbVar5 != (byte *)0x0) {
          __aeabi_memset(puVar25,pbVar5,0x30);
          puVar25 = (uint *)((int)puVar25 + (int)pbVar5);
        }
        pbVar5 = local_23c + (int)pbVar24;
        if ((int)local_260 < (int)pbVar5) {
          local_23c = local_260 + -(int)pbVar24;
        }
        if (local_23c != (byte *)0x0) {
          if ((int)local_260 < (int)pbVar5) {
            pbVar5 = local_260;
          }
          iVar18 = (int)pbVar24 - (int)pbVar5;
          puVar6 = puVar25;
          do {
            bVar29 = iVar18 != -1;
            iVar18 = iVar18 + 1;
            puVar27 = (uint *)((int)local_244 + 1);
            puVar25 = (uint *)((int)puVar6 + 1);
            *(byte *)puVar6 = (byte)*local_244;
            puVar6 = puVar25;
            local_244 = puVar27;
          } while (bVar29);
        }
        pbVar24 = local_260 + -(int)(local_23c + (int)pbVar24);
        local_264 = (byte *)0x3000001;
      }
      else {
        iVar18 = local_258 * 0x2000000;
        if (iVar18 < 0) {
          uVar7 = (600U - (int)local_240) % 3;
        }
        else {
          uVar7 = 0;
        }
        if (local_240 < local_23c) {
          pbVar24 = (byte *)0x0;
          puVar6 = (uint *)&local_1e8;
          do {
            puVar27 = puVar6;
            if ((iVar18 < 0) && (uVar7 = uVar7 + 1, uVar7 == 4)) {
              puVar27 = (uint *)((int)puVar6 + 1);
              *(byte *)puVar6 = *pbVar11;
              uVar7 = (local_258 & 0x7f) >> 6;
            }
            pbVar5 = (byte *)((int)local_244 + (int)pbVar24);
            pbVar24 = pbVar24 + 1;
            puVar6 = (uint *)((int)puVar27 + 1);
            *(byte *)puVar27 = *pbVar5;
          } while (pbVar24 < local_240);
          puVar25 = puVar6;
          if (local_260 != (byte *)0x0) {
            puVar25 = (uint *)((int)puVar27 + 2);
            *(byte *)((int)puVar27 + 1) = *(byte *)(DAT_000141c0 + 0x14156);
          }
          if (local_260 < local_23c + -(int)local_240) {
            local_23c = local_260 + (int)local_240;
          }
          if (pbVar24 < local_23c) {
            iVar18 = 0;
            do {
              *(byte *)((int)puVar25 + iVar18) = *(byte *)((int)local_244 + (int)(pbVar24 + iVar18))
              ;
              iVar18 = iVar18 + 1;
            } while (pbVar24 + iVar18 < local_23c);
            puVar25 = (uint *)((int)puVar25 + iVar18);
          }
          local_264 = (byte *)((int)puVar6 + (0x3000000 - (int)&local_1e8));
          pbVar24 = local_240 + ((int)local_260 - (int)local_23c);
        }
        else {
          pbVar5 = (byte *)0x0;
          iVar23 = -1;
          puVar25 = (uint *)&local_1e8;
          do {
            puVar6 = puVar25;
            if ((iVar18 < 0) && (uVar7 = uVar7 + 1, uVar7 == 4)) {
              puVar6 = (uint *)((int)puVar25 + 1);
              *(byte *)puVar25 = *pbVar11;
              uVar7 = (local_258 & 0x7f) >> 6;
            }
            iVar23 = iVar23 + -1;
            pbVar13 = (byte *)((int)local_244 + (int)pbVar5);
            pbVar5 = pbVar5 + 1;
            puVar25 = (uint *)((int)puVar6 + 1);
            *(byte *)puVar6 = *pbVar13;
          } while (pbVar5 < local_23c);
          if (pbVar5 < local_240) {
            pbVar13 = local_240 + -(int)pbVar5;
            if (-1 < iVar18) {
              if (local_240 != pbVar5 && ((uint)puVar25 & 3) != 0) {
                pbVar13 = (byte *)(2U - (int)puVar6 & 3);
                pbVar20 = local_240 + ~(uint)pbVar5;
                if (pbVar13 < local_240 + ~(uint)pbVar5) {
                  pbVar20 = pbVar13;
                }
                __aeabi_memset(puVar25,pbVar20 + 1,0x30);
                pbVar16 = pbVar24 + iVar23;
                if (pbVar16 < pbVar13) {
                  pbVar13 = pbVar16;
                }
                pbVar13 = pbVar24 + (~(uint)pbVar5 - (int)pbVar13);
                puVar25 = (uint *)((int)puVar6 + (int)(pbVar20 + 2));
              }
              if ((byte *)0x3 < pbVar13) {
                __aeabi_memset4(puVar25,(uint)pbVar13 & 0xfffffffc,0x30);
                puVar25 = (uint *)((int)puVar25 + ((uint)pbVar13 & 0xfffffffc));
                pbVar13 = (byte *)((uint)pbVar13 & 3);
              }
            }
            for (; pbVar13 != (byte *)0x0; pbVar13 = pbVar13 + -1) {
              while ((iVar18 < 0 && (uVar7 = uVar7 + 1, uVar7 == 4))) {
                uVar7 = 0;
                *(byte *)puVar25 = *pbVar11;
                puVar25 = (uint *)((int)puVar25 + 1);
                if (pbVar13 == (byte *)0x0) goto LAB_00014368;
              }
              *(byte *)puVar25 = 0x30;
              puVar25 = (uint *)((int)puVar25 + 1);
            }
          }
LAB_00014368:
          local_264 = (byte *)((int)puVar25 + (0x3000000 - (int)&local_1e8));
          if (local_260 == (byte *)0x0) {
            pbVar24 = (byte *)0x0;
          }
          else {
            *(byte *)puVar25 = *(byte *)(DAT_00014678 + 0x14382);
            puVar25 = (uint *)((int)puVar25 + 1);
            pbVar24 = local_260;
          }
        }
      }
      if ((int)(local_258 << 0x17) < 0) {
        uVar10 = local_258 & 0x400;
        uVar7 = local_258 >> 0x18;
        local_237._0_1_ = 0x20;
        local_238 = (byte)(uVar10 >> 10) ^ 1;
        if (uVar7 != 0) {
          iVar18 = 1;
          if (uVar10 == 0) {
            iVar18 = 2;
          }
          iVar23 = iVar18;
          if ((int)(local_258 << 0x14) < 0) {
            local_230[-(uVar10 >> 10) + -6] = *(undefined1 *)(DAT_00014684 + 0x1462e + uVar7);
            if ((local_258 & 0x1800) == 0x800) {
              iVar23 = iVar18 + 1;
              local_230[iVar18 + -7] = 0x69;
            }
          }
          else {
            local_230[-(uVar10 >> 10) + -6] = *(undefined1 *)(DAT_00014680 + 0x14622 + uVar7);
          }
          local_238 = (byte)iVar23;
        }
      }
LAB_00014746:
      local_260 = (byte *)0x0;
      local_23c = (byte *)((int)puVar25 - (int)&local_1e8);
      puVar25 = (uint *)&local_1e8;
    }
    goto LAB_000147c0;
  case 0x6e:
    **(undefined4 **)local_274 = (byte *)((local_24c - (int)param_3) + (int)puVar3);
    local_274 = (double *)((int)local_274 + 4);
LAB_00014d9a:
    param_4 = (uint *)((int)param_4 + 1);
    if (((uint)param_4 & 3) == 0) goto LAB_00014da8;
LAB_0001352a:
    uVar7 = (uint)(byte)*param_4;
    goto LAB_0001352c;
  case 0x6f:
    local_230 = (undefined1  [2])((ushort)local_230[1] << 8);
    if ((int)(local_258 << 0x1c) < 0) {
      local_230 = (undefined1  [2])0x3001;
    }
    local_254 = (byte *)(DAT_00013ed4 + 0x13bde);
    local_23c = (byte *)0x330;
    break;
  case 0x70:
    local_258 = local_258 & 0xffffffef;
    local_260 = (byte *)0x8;
  case 0x58:
  case 0x78:
    local_230 = (undefined1  [2])((ushort)local_230[1] << 8);
    local_23c = (byte *)0x440;
    local_254 = (byte *)(DAT_00013ed0 + 0x13ba0);
    if (uVar7 != 0x58) {
      local_254 = (byte *)(DAT_00013ecc + 0x13b9e);
    }
    if ((int)(local_258 << 0x1c) < 0) {
      if (uVar7 != 0x58) {
        uVar7 = 0x78;
      }
      local_230 = (undefined1  [2])0x3002;
      local_22e[0] = (undefined1)uVar7;
    }
    break;
  case 0x73:
    puVar25 = (uint *)(DAT_00013ed8 + 0x13cd8);
    if (*(uint **)local_274 != (uint *)0x0) {
      puVar25 = *(uint **)local_274;
    }
    uVar7 = (uint)local_260 | (int)local_260 >> 0x1f;
    if (((uint)puVar25 & 3) == 0) {
      puVar6 = puVar25;
LAB_0001475c:
      puVar1 = puVar6;
      uVar10 = uVar7;
      do {
        uVar17 = uVar10;
        puVar27 = puVar1;
        if (uVar17 < 4) {
          if (uVar17 == 0) goto LAB_0001479a;
          break;
        }
        puVar1 = puVar27 + 1;
        uVar10 = uVar17 - 4;
      } while (((0x1010100 - *puVar27 | *puVar27) & 0x80808080) == 0x80808080);
      uVar10 = 0;
      do {
        if (*(byte *)((int)puVar27 + uVar10) == 0) {
          puVar27 = (uint *)((int)puVar27 + uVar10);
          goto LAB_0001479a;
        }
        uVar10 = uVar10 + 1;
      } while (uVar17 != uVar10);
      puVar27 = (uint *)((int)puVar6 + uVar7);
LAB_0001479a:
    }
    else {
      puVar27 = (uint *)(uVar7 + (int)puVar25);
      if (local_260 != (byte *)0x0) {
        if ((byte)*puVar25 == 0) {
          puVar27 = puVar25;
        }
        else {
          puVar6 = (uint *)((int)puVar25 + 1);
          uVar10 = uVar7 - 1;
          if (((uint)puVar6 & 3) == 0) {
LAB_00014758:
            uVar7 = uVar10;
            goto LAB_0001475c;
          }
          if (uVar10 != 0) {
            if (*(byte *)puVar6 == 0) {
LAB_00014ec6:
              puVar27 = puVar6;
            }
            else {
              puVar6 = (uint *)((int)puVar25 + 2);
              uVar10 = uVar7 - 2;
              if (((uint)puVar6 & 3) == 0) goto LAB_00014758;
              if (uVar10 != 0) {
                if (*(byte *)puVar6 == 0) goto LAB_00014ec6;
                puVar6 = (uint *)((int)puVar25 + 3);
                uVar10 = uVar7 - 3;
                if (((uint)puVar6 & 3) == 0) goto LAB_00014758;
                if (uVar10 != 0) {
                  if (*(byte *)puVar6 == 0) goto LAB_00014ec6;
                  if (((uint)puVar25 & 3) == 0) {
                    puVar6 = puVar25 + 1;
                    uVar7 = uVar7 - 4;
                    goto LAB_0001475c;
                  }
                  if (uVar7 - 4 != 0) {
                    puVar27 = puVar25 + 1;
                  }
                }
              }
            }
          }
        }
      }
    }
    local_23c = (byte *)((int)puVar27 - (int)puVar25);
    local_230 = (undefined1  [2])((ushort)local_230[1] << 8);
    local_274 = (double *)((int)local_274 + 4);
    goto LAB_000147b0;
  }
  if ((int)(local_258 << 0x1a) < 0) {
    puVar25 = (uint *)((int)local_274 + 7U & 0xfffffff8);
    uVar7 = puVar25[1];
    pdVar9 = (double *)(puVar25 + 2);
    uVar10 = *puVar25;
  }
  else {
    uVar7 = 0;
    pdVar9 = (double *)((int)local_274 + 4);
    uVar10 = *(uint *)local_274;
  }
  local_238 = 0;
  local_240 = (byte *)0x0;
  puVar25 = &local_28;
  local_274 = pdVar9;
  if ((uVar10 == 0 && uVar7 == 0) &&
     (local_230 = (undefined1  [2])((ushort)local_230 & 0xff00), local_260 == (byte *)0x0)) {
    local_23c = (byte *)0x0;
LAB_000147b0:
    local_238 = 0;
    local_240 = (byte *)0x0;
    local_264 = (byte *)0x0;
    pbVar24 = (byte *)0x0;
    local_260 = (byte *)0x0;
  }
  else {
    while( true ) {
      puVar6 = puVar25;
      uVar17 = uVar10 & ~(-1 << ((uint)local_23c >> 8 & 0xff));
      uVar10 = uVar10 >> ((uint)local_23c >> 8 & 0xff) |
               uVar7 << (0x20 - ((uint)local_23c >> 8) & 0xff);
      uVar4 = ((uint)local_23c >> 8) - 0x20;
      if (-1 < (int)uVar4) {
        uVar10 = uVar7 >> (uVar4 & 0xff);
      }
      uVar7 = uVar7 >> ((uint)local_23c >> 8 & 0xff);
      if (-1 < (int)uVar4) {
        uVar7 = 0;
      }
      puVar25 = (uint *)((int)puVar6 + -1);
      *(byte *)puVar25 = *(byte *)((int)local_254 + uVar17);
      if ((uVar10 == 0 && uVar7 == 0) &&
         (pbVar5 = (byte *)((int)&local_28 - (int)puVar25), (int)local_260 <= (int)pbVar5)) break;
      if ((int)(local_258 << 0x19) < 0) {
        local_23c = (byte *)((int)local_23c + 1);
        if ((((uint)local_23c ^ (uint)local_23c >> 4) & 0xf) == 0) {
          local_23c = (byte *)((uint)local_23c & 0xfffffff0);
          puVar25 = (uint *)((int)puVar6 + -2);
          *(byte *)puVar25 = *pbVar11;
        }
      }
    }
    pbVar24 = (byte *)0x0;
    local_264 = pbVar5 + ((uint)local_23c & 0xf0) * 0x100000;
    local_23c = pbVar5;
  }
LAB_000147c0:
  pbVar5 = (byte *)(uint)local_230[0];
  if ((int)local_260 <= (int)local_23c) {
    local_260 = local_23c;
  }
  pbVar13 = local_260 + (int)pbVar24 + (int)pbVar5 + local_238;
  local_260 = local_260 + -(int)local_23c;
  if ((int)pbVar14 <= (int)pbVar13) {
    pbVar14 = pbVar13;
  }
  local_254 = pbVar14 + -(int)pbVar13;
  if ((local_258 & 1) == 0) {
    if ((int)(local_258 << 0x1b) < 0) {
      if ((int)local_260 < (int)local_254) {
        local_260 = local_254;
      }
      local_254 = (byte *)0x0;
    }
    else {
      local_258 = 0;
    }
  }
  if (local_254 + (int)local_260 != (byte *)0x0) {
    if (((local_258 & 1) == 0) && (0 < (int)local_254)) {
      do {
        pbVar14 = (byte *)((int)param_3 + (0x200 - (int)puVar3));
        if ((int)local_254 < (int)pbVar14) {
          pbVar14 = local_254;
        }
        if (param_1 == (code *)0x0) {
          pbVar14 = local_254;
        }
        if ((pbVar14 == (byte *)0x0) || (((uint)puVar3 & 3) == 0)) {
          pbVar5 = pbVar14;
        }
        else {
          pbVar13 = pbVar14 + -1;
          pbVar5 = (byte *)(~(uint)puVar3 & 3);
          if (pbVar13 <= (byte *)(~(uint)puVar3 & 3)) {
            pbVar5 = pbVar13;
          }
          __aeabi_memset(puVar3,pbVar5 + 1,0x20);
          puVar3 = (uint *)((int)puVar3 + (int)(pbVar5 + 1));
          pbVar5 = pbVar13 + -(int)pbVar5;
        }
        if (3 < (int)pbVar5) {
          pbVar13 = pbVar5 + -7;
          if (pbVar5 < (byte *)0x7) {
            pbVar13 = (byte *)0x0;
          }
          iVar18 = ((uint)(pbVar13 + 3) & 0xfffffffc) + 4;
          __aeabi_memset4(puVar3,iVar18,0x20);
          puVar3 = (uint *)((int)puVar3 + iVar18);
          pbVar5 = pbVar5 + (-4 - ((uint)(pbVar13 + 3) & 0xfffffffc));
        }
        if (pbVar5 != (byte *)0x0) {
          __aeabi_memset(puVar3,pbVar5,0x20);
          puVar3 = (uint *)((int)puVar3 + (int)pbVar5);
        }
        if (param_1 != (code *)0x0) {
          iVar18 = (int)puVar3 - (int)param_3;
          if (0x1fe < iVar18) {
            puVar3 = (uint *)(*param_1)(param_3,param_2,iVar18);
            local_24c = local_24c + iVar18;
            if (puVar3 == (uint *)0x0) goto LAB_00014f14;
            param_3 = puVar3;
          }
        }
        local_254 = local_254 + -(int)pbVar14;
      } while (0 < (int)local_254);
      pbVar5 = (byte *)(uint)local_230[0];
    }
    local_244 = puVar8;
    while (pbVar5 != (byte *)0x0) {
      pbVar14 = (byte *)((int)param_3 + (0x200 - (int)puVar3));
      if ((int)pbVar5 <= (int)pbVar14) {
        pbVar14 = pbVar5;
      }
      if (param_1 == (code *)0x0) {
        pbVar14 = pbVar5;
      }
      local_230[0] = (char)pbVar5 - (char)pbVar14;
      for (; pbVar14 != (byte *)0x0; pbVar14 = pbVar14 + -1) {
        *(byte *)puVar3 = (byte)*local_244;
        puVar3 = (uint *)((int)puVar3 + 1);
        local_244 = (uint *)((int)local_244 + 1);
      }
      if (param_1 != (code *)0x0) {
        iVar18 = (int)puVar3 - (int)param_3;
        if (0x1fe < iVar18) {
          puVar3 = (uint *)(*param_1)(param_3,param_2,iVar18);
          local_24c = local_24c + iVar18;
          if (puVar3 == (uint *)0x0) goto LAB_00014f14;
          param_3 = puVar3;
        }
      }
      pbVar5 = (byte *)(uint)local_230[0];
    }
    iVar18 = local_258 * 0x2000000;
    uVar7 = (uint)local_264 >> 0x18;
    if (iVar18 < 0) {
      FUN_0001b528(local_260 + ((uint)local_264 & 0xffffff),uVar7 + 1);
      uVar10 = uVar7 - extraout_r1;
    }
    else {
      uVar10 = 0;
    }
    puVar6 = puVar8;
    pbVar14 = local_23c;
    if ((int)local_260 < 1) goto joined_r0x00014b84;
    do {
      pbVar14 = (byte *)((int)param_3 + (0x200 - (int)puVar3));
      if ((int)local_260 < (int)pbVar14) {
        pbVar14 = local_260;
      }
      if (param_1 == (code *)0x0) {
        pbVar14 = local_260;
      }
      pbVar5 = pbVar14;
      if (-1 < iVar18) {
        if ((pbVar14 != (byte *)0x0) && (((uint)puVar3 & 3) != 0)) {
          pbVar13 = pbVar14 + -1;
          pbVar5 = (byte *)(~(uint)puVar3 & 3);
          if (pbVar13 <= (byte *)(~(uint)puVar3 & 3)) {
            pbVar5 = pbVar13;
          }
          __aeabi_memset(puVar3,pbVar5 + 1,0x30);
          puVar3 = (uint *)((int)puVar3 + (int)(pbVar5 + 1));
          pbVar5 = pbVar13 + -(int)pbVar5;
        }
        if (3 < (int)pbVar5) {
          pbVar13 = pbVar5 + -7;
          if (pbVar5 < (byte *)0x7) {
            pbVar13 = (byte *)0x0;
          }
          iVar23 = ((uint)(pbVar13 + 3) & 0xfffffffc) + 4;
          __aeabi_memset4(puVar3,iVar23,0x30);
          puVar3 = (uint *)((int)puVar3 + iVar23);
          pbVar5 = pbVar5 + (-4 - ((uint)(pbVar13 + 3) & 0xfffffffc));
        }
      }
      for (; pbVar5 != (byte *)0x0; pbVar5 = pbVar5 + -1) {
        if (iVar18 < 0) {
          bVar2 = *pbVar11;
          if (uVar10 != uVar7) {
            bVar2 = 0x30;
          }
          uVar17 = 0;
          if (uVar10 != uVar7) {
            uVar17 = uVar10 + 1;
          }
        }
        else {
          bVar2 = 0x30;
          uVar17 = uVar10;
        }
        *(byte *)puVar3 = bVar2;
        puVar3 = (uint *)((int)puVar3 + 1);
        uVar10 = uVar17;
      }
      if ((param_1 != (code *)0x0) && (iVar23 = (int)puVar3 - (int)param_3, 0x1fe < iVar23)) {
        puVar3 = (uint *)(*param_1)(param_3,param_2,iVar23);
        local_24c = local_24c + iVar23;
        if (puVar3 == (uint *)0x0) goto LAB_00014f14;
        param_3 = puVar3;
      }
      local_260 = local_260 + -(int)pbVar14;
    } while (0 < (int)local_260);
    pbVar5 = (byte *)(uint)local_230[0];
  }
  local_244 = puVar8;
  while( true ) {
    puVar6 = local_244;
    pbVar14 = local_23c;
    if (pbVar5 == (byte *)0x0) break;
    pbVar14 = (byte *)((int)param_3 + (0x200 - (int)puVar3));
    if ((int)pbVar5 <= (int)pbVar14) {
      pbVar14 = pbVar5;
    }
    if (param_1 == (code *)0x0) {
      pbVar14 = pbVar5;
    }
    local_230[0] = (char)pbVar5 - (char)pbVar14;
    for (; pbVar14 != (byte *)0x0; pbVar14 = pbVar14 + -1) {
      *(byte *)puVar3 = (byte)*local_244;
      puVar3 = (uint *)((int)puVar3 + 1);
      local_244 = (uint *)((int)local_244 + 1);
    }
    if (param_1 != (code *)0x0) {
      iVar18 = (int)puVar3 - (int)param_3;
      if (0x1fe < iVar18) {
        puVar3 = (uint *)(*param_1)(param_3,param_2,iVar18);
        local_24c = local_24c + iVar18;
        if (puVar3 == (uint *)0x0) goto LAB_00014f14;
        param_3 = puVar3;
      }
    }
    pbVar5 = (byte *)(uint)local_230[0];
  }
joined_r0x00014b84:
  local_244 = puVar6;
  if (pbVar14 != (byte *)0x0) {
    do {
      pbVar11 = (byte *)((int)param_3 - (int)puVar3);
      pbVar5 = pbVar11 + 0x200;
      if ((int)pbVar14 < (int)(pbVar11 + 0x200)) {
        pbVar5 = pbVar14;
      }
      if (param_1 == (code *)0x0) {
        pbVar5 = pbVar14;
      }
      pbVar13 = pbVar5;
      puVar6 = puVar3;
      puVar27 = puVar25;
      if (3 < (int)pbVar5) {
        do {
          puVar25 = puVar27 + 1;
          puVar3 = puVar6 + 1;
          *puVar6 = *puVar27;
          pbVar11 = pbVar13 + -4;
          bVar29 = (byte *)0x7 < pbVar13;
          pbVar13 = pbVar11;
          puVar6 = puVar3;
          puVar27 = puVar25;
        } while (bVar29);
      }
      if (pbVar13 != (byte *)0x0) {
        bVar29 = pbVar13 == (byte *)0x10;
        if ((byte *)0xf < pbVar13) {
          pbVar11 = (byte *)((int)puVar3 - (int)puVar25);
          bVar29 = pbVar11 == (byte *)0xf;
        }
        if (((byte *)0xf < pbVar13 && (byte *)0xe < pbVar11) && !bVar29) {
          pbVar16 = (byte *)((uint)pbVar13 & 0xfffffff0);
          pbVar11 = (byte *)((uint)pbVar13 & 0xf);
          puVar27 = (uint *)((int)puVar25 + (int)pbVar16);
          puVar6 = (uint *)((int)puVar3 + (int)pbVar16);
          pbVar20 = pbVar16;
          do {
            uVar32 = *(undefined8 *)puVar25;
            uVar34 = *(undefined8 *)(puVar25 + 2);
            puVar25 = puVar25 + 4;
            pbVar20 = pbVar20 + -0x10;
            *(undefined8 *)puVar3 = uVar32;
            *(undefined8 *)(puVar3 + 2) = uVar34;
            puVar3 = puVar3 + 4;
          } while (pbVar20 != (byte *)0x0);
          puVar3 = puVar6;
          puVar25 = puVar27;
          if (pbVar13 == pbVar16) goto LAB_00014c04;
        }
        else {
          puVar6 = puVar3;
          pbVar11 = pbVar13;
          puVar27 = puVar25;
        }
        do {
          pbVar11 = pbVar11 + -1;
          *(byte *)puVar6 = (byte)*puVar27;
          puVar3 = (uint *)((int)puVar6 + 1);
          puVar6 = (uint *)((int)puVar6 + 1);
          puVar25 = (uint *)((int)puVar27 + 1);
          puVar27 = (uint *)((int)puVar27 + 1);
        } while (pbVar11 != (byte *)0x0);
      }
LAB_00014c04:
      if ((param_1 == (code *)0x0) || (iVar18 = (int)puVar3 - (int)param_3, iVar18 < 0x1ff))
      goto LAB_00014b88;
      puVar3 = (uint *)(*param_1)(param_3,param_2,iVar18);
      local_24c = local_24c + iVar18;
      if (puVar3 == (uint *)0x0) goto LAB_00014f14;
      pbVar14 = pbVar14 + -(int)pbVar5;
      param_3 = puVar3;
      if (pbVar14 == (byte *)0x0) break;
    } while( true );
  }
  goto joined_r0x00014c42;
LAB_00013b3e:
  local_238 = 0;
  local_230 = (undefined1  [2])((ushort)local_230 & 0xff00);
  if ((int)(local_258 << 0x18) < 0) {
    local_230[1] = 0x2d;
LAB_00014286:
    local_230[0] = 1;
  }
  else {
    if ((int)(local_258 << 0x1d) < 0) {
      local_230[1] = 0x20;
      goto LAB_00014286;
    }
    if ((int)(local_258 << 0x1e) < 0) {
      local_230[1] = 0x2b;
      goto LAB_00014286;
    }
  }
  pbVar24 = (byte *)0x0;
  iVar18 = iVar18 + (uint)(*(char *)((int)auStack_228 + iVar18) == '0' && iVar18 != 0x200);
  local_23c = (byte *)(0x200 - iVar18);
  puVar25 = (uint *)((int)auStack_228 + iVar18);
  if (iVar18 == 0x200) {
    local_23c = (byte *)0x1;
    puVar25 = (uint *)&local_29;
    local_29 = 0x30;
  }
  local_264 = local_23c + 0x3000000;
  local_260 = (byte *)((uint)local_260 & ~((int)local_260 >> 0x1f));
  goto LAB_000147c0;
LAB_00014b88:
  puVar6 = local_244;
  pbVar14 = pbVar14 + -(int)pbVar5;
  goto joined_r0x00014b84;
joined_r0x00014c42:
  puVar25 = &local_237;
  if (pbVar24 == (byte *)0x0) goto joined_r0x00014c4e;
  pbVar14 = (byte *)((int)param_3 + (0x200 - (int)puVar3));
  if ((int)pbVar24 < (int)pbVar14) {
    pbVar14 = pbVar24;
  }
  if (param_1 == (code *)0x0) {
    pbVar14 = pbVar24;
  }
  if ((pbVar14 == (byte *)0x0) || (((uint)puVar3 & 3) == 0)) {
    pbVar11 = pbVar14;
  }
  else {
    pbVar5 = pbVar14 + -1;
    pbVar11 = (byte *)(~(uint)puVar3 & 3);
    if (pbVar5 <= (byte *)(~(uint)puVar3 & 3)) {
      pbVar11 = pbVar5;
    }
    __aeabi_memset(puVar3,pbVar11 + 1,0x30);
    puVar3 = (uint *)((int)puVar3 + (int)(pbVar11 + 1));
    pbVar11 = pbVar5 + -(int)pbVar11;
  }
  if (3 < (int)pbVar11) {
    pbVar5 = pbVar11 + -7;
    if (pbVar11 < (byte *)0x7) {
      pbVar5 = (byte *)0x0;
    }
    iVar18 = ((uint)(pbVar5 + 3) & 0xfffffffc) + 4;
    __aeabi_memset4(puVar3,iVar18,0x30);
    puVar3 = (uint *)((int)puVar3 + iVar18);
    pbVar11 = pbVar11 + (-4 - ((uint)(pbVar5 + 3) & 0xfffffffc));
  }
  if (pbVar11 != (byte *)0x0) {
    __aeabi_memset(puVar3,pbVar11,0x30);
    puVar3 = (uint *)((int)puVar3 + (int)pbVar11);
  }
  if (param_1 != (code *)0x0) {
    iVar18 = (int)puVar3 - (int)param_3;
    if (0x1fe < iVar18) {
      puVar3 = (uint *)(*param_1)(param_3,param_2,iVar18);
      local_24c = local_24c + iVar18;
      if (puVar3 == (uint *)0x0) goto LAB_00014f14;
      param_3 = puVar3;
    }
  }
  pbVar24 = pbVar24 + -(int)pbVar14;
  goto joined_r0x00014c42;
joined_r0x00014c4e:
  local_244 = puVar25;
  pbVar14 = (byte *)(uint)local_238;
  if (pbVar14 != (byte *)0x0) {
    pbVar11 = (byte *)((int)param_3 + (0x200 - (int)puVar3));
    if ((int)pbVar14 <= (int)pbVar11) {
      pbVar11 = pbVar14;
    }
    if (param_1 == (code *)0x0) {
      pbVar11 = pbVar14;
    }
    local_238 = local_238 - (char)pbVar11;
    for (; pbVar11 != (byte *)0x0; pbVar11 = pbVar11 + -1) {
      *(byte *)puVar3 = (byte)*local_244;
      puVar3 = (uint *)((int)puVar3 + 1);
      local_244 = (uint *)((int)local_244 + 1);
    }
    puVar25 = local_244;
    if (param_1 != (code *)0x0) {
      iVar18 = (int)puVar3 - (int)param_3;
      if (0x1fe < iVar18) {
        puVar3 = (uint *)(*param_1)(param_3,param_2,iVar18);
        local_24c = local_24c + iVar18;
        puVar25 = local_244;
        param_3 = puVar3;
        if (puVar3 == (uint *)0x0) goto LAB_00014f14;
      }
    }
    goto joined_r0x00014c4e;
  }
  if (((local_258 & 1) != 0) && (0 < (int)local_254)) {
    do {
      pbVar14 = (byte *)((int)param_3 + (0x200 - (int)puVar3));
      if ((int)local_254 < (int)pbVar14) {
        pbVar14 = local_254;
      }
      if (param_1 == (code *)0x0) {
        pbVar14 = local_254;
      }
      pbVar11 = pbVar14;
      if ((pbVar14 != (byte *)0x0) && (((uint)puVar3 & 3) != 0)) {
        pbVar24 = pbVar14 + -1;
        pbVar11 = (byte *)(~(uint)puVar3 & 3);
        if (pbVar24 <= (byte *)(~(uint)puVar3 & 3)) {
          pbVar11 = pbVar24;
        }
        __aeabi_memset(puVar3,pbVar11 + 1,0x20);
        puVar3 = (uint *)((int)puVar3 + (int)(pbVar11 + 1));
        pbVar11 = pbVar24 + -(int)pbVar11;
      }
      if (3 < (int)pbVar11) {
        pbVar24 = pbVar11 + -7;
        if (pbVar11 < (byte *)0x7) {
          pbVar24 = (byte *)0x0;
        }
        iVar18 = ((uint)(pbVar24 + 3) & 0xfffffffc) + 4;
        __aeabi_memset4(puVar3,iVar18,0x20);
        puVar3 = (uint *)((int)puVar3 + iVar18);
        pbVar11 = pbVar11 + (-4 - ((uint)(pbVar24 + 3) & 0xfffffffc));
      }
      if (pbVar11 != (byte *)0x0) {
        __aeabi_memset(puVar3,pbVar11,0x20);
        puVar3 = (uint *)((int)puVar3 + (int)pbVar11);
      }
      if (param_1 != (code *)0x0) {
        iVar18 = (int)puVar3 - (int)param_3;
        if (0x1fe < iVar18) {
          puVar3 = (uint *)(*param_1)(param_3,param_2,iVar18);
          local_24c = local_24c + iVar18;
          if (puVar3 == (uint *)0x0) goto LAB_00014f14;
          param_3 = puVar3;
        }
      }
      local_254 = local_254 + -(int)pbVar14;
    } while (local_254 != (byte *)0x0);
  }
  goto LAB_00014d9a;
}


// ==== FUN_00014f40 @ 00014f40

uint FUN_00014f40(uint param_1,uint param_2,int *param_3,int *param_4,undefined1 *param_5,
                 int *param_6,uint param_7)

{
  uint *puVar1;
  char *pcVar2;
  uint uVar3;
  int iVar4;
  uint uVar5;
  int iVar6;
  int iVar7;
  undefined1 *puVar8;
  uint uVar9;
  uint uVar10;
  bool bVar11;
  double dVar12;
  double dVar13;
  double dVar14;
  double dVar15;
  double dVar16;
  double dVar17;
  double dVar18;
  longlong lVar19;
  longlong lVar20;
  ulonglong uVar21;
  ulonglong uVar22;
  int local_48;
  
  iVar4 = DAT_00015224;
  iVar7 = DAT_00015220;
  uVar3 = (param_2 & 0x7fffffff) >> 0x14;
  if (uVar3 == 0) {
    if ((double)CONCAT44(param_2,param_1) == 0.0) {
      *param_3 = (int)param_5;
      iVar7 = 1;
      *param_6 = 1;
      *param_5 = 0x30;
      goto LAB_0001550e;
    }
    if (-1 < (int)(param_2 << 0xc)) {
      uVar9 = 0x80000;
      uVar3 = 0;
      uVar10 = 0;
      do {
        uVar5 = uVar9 & 1;
        uVar9 = uVar9 >> 1;
        uVar10 = (uint)(uVar5 != 0) << 0x1f | uVar10 >> 1;
        uVar3 = uVar3 - 1;
      } while ((uVar9 & param_2) == 0 && (uVar10 & param_1) == 0);
      goto LAB_00014fce;
    }
    iVar7 = -0x3ff;
LAB_00014ff4:
    local_48 = -((uint)(iVar7 * -0x269) >> 0xb);
  }
  else {
    if (uVar3 == 0x7ff) {
      *param_6 = 0x7000;
      iVar4 = iVar4 + 0x14f7c;
      if (param_1 == 0 && (param_2 & 0xfffff) == 0) {
        iVar4 = iVar7 + 0x14f7e;
      }
      *param_3 = iVar4;
      iVar7 = 3;
      goto LAB_0001550e;
    }
LAB_00014fce:
    iVar7 = uVar3 - 0x3ff;
    if ((int)uVar3 < 0x3ff) goto LAB_00014ff4;
    local_48 = ((uint)(iVar7 * 0x4d1) >> 0xc) + 1;
  }
  dVar13 = ABS((double)CONCAT44(param_2,param_1));
  uVar3 = 0x12 - local_48;
  if (uVar3 < 0x17) {
    dVar14 = *(double *)(DAT_00015228 + 0x1501e + uVar3 * 8);
    dVar12 = dVar13 * dVar14;
    dVar15 = ABS((double)((ulonglong)dVar13 & 0xfffffffff8000000));
    dVar16 = (double)(((ulonglong)dVar14 & 0xffffffff00000000) +
                     (ulonglong)(SUB84(dVar14,0) & 0xf8000000));
    dVar13 = (dVar15 * dVar16 - dVar12) + dVar15 * (dVar14 - dVar16) + (dVar13 - dVar15) * dVar16 +
             (dVar13 - dVar15) * (dVar14 - dVar16);
  }
  else {
    if ((int)uVar3 < 0) {
      uVar3 = -uVar3;
    }
    uVar10 = 0xd;
    if (uVar3 * 0x2c9 >> 0xe < 0xd) {
      uVar10 = uVar3 * 0x2c9 >> 0xe;
    }
    iVar7 = uVar10 * -0x17 + uVar3;
    dVar12 = dVar13;
    if (local_48 < 0x13) {
      if (iVar7 == 0) {
        dVar13 = 0.0;
      }
      else {
        iVar4 = iVar7;
        if (0x15 < iVar7) {
          iVar4 = 0x16;
        }
        dVar14 = *(double *)(DAT_00015234 + 0x15108 + iVar4 * 8);
        dVar12 = dVar13 * dVar14;
        dVar15 = ABS((double)((ulonglong)dVar13 & 0xfffffffff8000000));
        dVar16 = (double)(((ulonglong)dVar14 & 0xffffffff00000000) +
                         (ulonglong)(SUB84(dVar14,0) & 0xf8000000));
        dVar13 = (dVar15 * dVar16 - dVar12) + dVar15 * (dVar14 - dVar16) +
                 (dVar13 - dVar15) * dVar16 + (dVar13 - dVar15) * (dVar14 - dVar16);
        if (0x16 < iVar7) {
          dVar15 = dVar12 + dVar13;
          dVar16 = *(double *)(DAT_00015234 + 0x15108 + (iVar7 - iVar4) * 8);
          dVar14 = dVar15 - dVar12;
          dVar12 = dVar15 * dVar16;
          dVar17 = (double)((ulonglong)dVar16 & 0xfffffffff8000000);
          dVar18 = (double)(((ulonglong)dVar15 & 0xffffffff00000000) +
                           (ulonglong)(SUB84(dVar15,0) & 0xf8000000));
          dVar13 = (dVar18 * dVar17 - dVar12) + dVar18 * (dVar16 - dVar17) +
                   (dVar15 - dVar18) * dVar17 + (dVar15 - dVar18) * (dVar16 - dVar17) +
                   dVar16 * (dVar13 - dVar14);
        }
      }
      if (uVar3 < 0x17) goto LAB_00015294;
      dVar15 = dVar12 + dVar13;
      iVar4 = uVar10 * 8 + -8;
      dVar16 = *(double *)(DAT_00015530 + 0x151c2 + iVar4);
      dVar14 = dVar15 - dVar12;
      dVar12 = dVar15 * dVar16;
      dVar13 = dVar13 - dVar14;
      dVar14 = (double)((ulonglong)dVar16 & 0xfffffffff8000000);
      dVar17 = (double)(((ulonglong)dVar15 & 0xffffffff00000000) +
                       (ulonglong)(SUB84(dVar15,0) & 0xf8000000));
      dVar18 = dVar16 - dVar14;
      iVar7 = DAT_00015534 + 0x151f8;
    }
    else {
      if (iVar7 == 0) {
        dVar13 = 0.0;
      }
      else {
        iVar7 = iVar7 * 8 + -8;
        dVar14 = *(double *)(DAT_0001522c + 0x15098 + iVar7);
        dVar12 = dVar13 * dVar14;
        dVar15 = ABS((double)((ulonglong)dVar13 & 0xfffffffff8000000));
        dVar16 = (double)(((ulonglong)dVar14 & 0xffffffff00000000) +
                         (ulonglong)(SUB84(dVar14,0) & 0xf8000000));
        dVar13 = (dVar15 * dVar16 - dVar12) + dVar15 * (dVar14 - dVar16) +
                 (dVar13 - dVar15) * dVar16 + (dVar13 - dVar15) * (dVar14 - dVar16) +
                 dVar13 * *(double *)(iVar7 + DAT_00015230 + 0x150ca);
      }
      if (uVar3 < 0x17) goto LAB_00015294;
      dVar15 = dVar12 + dVar13;
      iVar4 = uVar10 * 8 + -8;
      dVar16 = *(double *)(DAT_00015528 + 0x15172 + iVar4);
      dVar14 = dVar15 - dVar12;
      dVar12 = dVar15 * dVar16;
      dVar13 = dVar13 - dVar14;
      dVar14 = (double)((ulonglong)dVar16 & 0xfffffffff8000000);
      dVar17 = (double)(((ulonglong)dVar15 & 0xffffffff00000000) +
                       (ulonglong)(SUB84(dVar15,0) & 0xf8000000));
      dVar18 = dVar16 - dVar14;
      iVar7 = DAT_0001552c + 0x151a8;
    }
    dVar13 = dVar13 * dVar16 + dVar15 * *(double *)(iVar7 + iVar4) +
             (dVar17 * dVar14 - dVar12) + dVar17 * dVar18 + (dVar15 - dVar17) * dVar14 +
             (dVar15 - dVar17) * dVar18;
  }
LAB_00015294:
  dVar14 = dVar12 + dVar13;
  lVar19 = FUN_0001b468(SUB84(dVar14,0),(int)((ulonglong)dVar14 >> 0x20));
  dVar15 = (double)FUN_0001b4e8();
  dVar16 = dVar14 - dVar15;
  dVar13 = (dVar13 - (dVar14 - dVar12)) +
           dVar16 + ((dVar14 - (dVar16 - (dVar16 - dVar14))) - ((dVar16 - dVar14) + dVar15));
  lVar20 = FUN_0001b468(SUB84(dVar13,0),(int)((ulonglong)dVar13 >> 0x20));
  uVar22 = lVar19 + lVar20;
  uVar10 = (uint)uVar22;
  uVar3 = (uint)(uVar22 >> 0x20);
  if (0xde0b6b2 < uVar3 && (uint)(uVar10 < 0xa7640000) <= 0xde0b6b3 - uVar3) {
    local_48 = local_48 + 1;
  }
  uVar9 = local_48 + param_7;
  if ((int)param_7 < 0) {
    uVar9 = (param_7 & 0x7ffffff) + 1;
  }
  if (uVar9 < 0x18) {
    iVar7 = 1;
    if ((uint)(uVar10 < 1000000000) <= -uVar3) {
      iVar7 = 10;
    }
    puVar1 = (uint *)(DAT_00015538 + 0x1532e + iVar7 * 8);
    iVar7 = -iVar7;
    do {
      uVar5 = puVar1[1];
      if (uVar3 <= uVar5 && (uint)(*puVar1 <= uVar10) <= uVar3 - uVar5) {
        if ((uVar9 < (uint)-iVar7) && (-(iVar7 + uVar9) < 0x17 || iVar7 + uVar9 == -0x17)) {
          uVar3 = (puVar1 + uVar9 * -2)[1];
          lVar19 = uVar22 + CONCAT44(uVar3 >> 1,
                                     (uint)((uVar3 & 1) != 0) << 0x1f | puVar1[uVar9 * -2] >> 1);
          uVar3 = (uint)((ulonglong)lVar19 >> 0x20);
          if (uVar5 < uVar3 || uVar3 - uVar5 < (uint)(*puVar1 <= (uint)lVar19)) {
            local_48 = local_48 + 1;
          }
          uVar22 = FUN_0001b728();
        }
        break;
      }
      puVar1 = puVar1 + 2;
      bVar11 = iVar7 != -0x13;
      iVar7 = iVar7 + -1;
    } while (bVar11);
  }
  uVar3 = (uint)uVar22;
  if (uVar22 == 0) {
    uVar3 = 0;
  }
  else {
    if (0xffffffff < (longlong)uVar22) {
      do {
        uVar10 = (uint)(uVar22 >> 0x20);
        uVar5 = (uint)uVar22;
        uVar21 = FUN_0001b728(uVar5,uVar10,1000,0);
        uVar3 = (uint)uVar21;
        uVar9 = (uint)(uVar21 * 1000);
        if (uVar5 != uVar9 || uVar10 - (int)(uVar21 * 1000 >> 0x20) != (uint)(uVar5 < uVar9))
        goto LAB_00015412;
        uVar22 = uVar21;
      } while (0x7c < uVar10 >> 3);
    }
    uVar10 = uVar3 * 0x26e978d5 >> 3 | uVar3 * -0x60000000;
    while (uVar10 < 0x418938) {
      uVar3 = uVar3 / 1000;
      uVar10 = uVar3 * 0x26e978d5 >> 3 | uVar3 * -0x60000000;
    }
  }
  uVar22 = (ulonglong)uVar3;
LAB_00015412:
  puVar8 = param_5 + 0x3e;
  param_5 = param_5 + 0x40;
  iVar7 = 0;
  do {
    iVar4 = (int)(uVar22 >> 0x20);
    uVar3 = (uint)uVar22;
    if ((int)(-(uint)(99999999 < uVar3) - iVar4) < 0 ==
        (SBORROW4(0,iVar4) != SBORROW4(-iVar4,(uint)(99999999 < uVar3)))) {
      iVar6 = 0;
      if (uVar3 == 0) break;
      uVar22 = 0;
LAB_000154ae:
      iVar6 = 0;
      do {
        iVar4 = iVar6;
        *(undefined2 *)(puVar8 + iVar4) =
             *(undefined2 *)(DAT_0001553c + (uVar3 % 100) * 2 + 0x154be);
        iVar6 = iVar4 + -2;
        bVar11 = 99 < uVar3;
        uVar3 = uVar3 / 100;
      } while (bVar11);
      iVar7 = iVar7 - iVar6;
      if (uVar22 == 0) break;
      if (iVar4 != -6) goto LAB_00015438;
    }
    else {
      iVar6 = 0;
      uVar22 = FUN_0001b728(uVar3,iVar4,100000000,0);
      uVar3 = uVar3 + (int)uVar22 * -100000000;
      if (uVar3 != 0) goto LAB_000154ae;
LAB_00015438:
      __aeabi_memset(param_5 + -8,iVar6 + 8,0x30);
      iVar7 = iVar7 + iVar6 + 8;
    }
    puVar8 = puVar8 + -8;
    param_5 = param_5 + -8;
  } while( true );
  pcVar2 = param_5 + iVar6;
  if (iVar7 == 0) {
    iVar7 = 0;
  }
  else if (*pcVar2 == '0') {
    iVar7 = iVar7 + -1;
    pcVar2 = pcVar2 + 1;
  }
  *param_3 = (int)pcVar2;
  *param_6 = local_48;
LAB_0001550e:
  *param_4 = iVar7;
  return param_2 >> 0x1f;
}


// ==== stbsp_sprintf @ 00015540

void stbsp_sprintf(undefined4 param_1,undefined4 param_2,undefined4 param_3,undefined4 param_4)

{
  int iVar1;
  undefined4 local_8;
  undefined4 uStack_4;
  
  iVar1 = **(int **)(DAT_0001558c + 0x1555a);
  local_8 = param_3;
  uStack_4 = param_4;
  stbsp_vsprintfcb(0,0,param_1,param_2,&local_8);
  if (**(int **)(DAT_00015590 + 0x15576) != iVar1) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== stbsp_vsnprintf @ 00015594

void stbsp_vsnprintf(undefined1 *param_1,undefined1 *param_2,undefined4 param_3,undefined4 param_4)

{
  undefined1 *puVar1;
  undefined1 *local_220;
  undefined1 *puStack_21c;
  undefined4 local_218;
  undefined1 auStack_214 [512];
  int local_14;
  
  local_14 = **(int **)(DAT_00015630 + 0x155a8);
  if (param_1 == (undefined1 *)0x0 && param_2 == (undefined1 *)0x0) {
    local_218 = 0;
    stbsp_vsprintfcb(DAT_00015634 + 0x155e2,&local_220,auStack_214,param_3,param_4);
    goto LAB_00015614;
  }
  local_218 = 0;
  if ((int)param_2 < 0) {
    puStack_21c = (undefined1 *)0x0;
    local_220 = param_2;
LAB_000155f2:
    puVar1 = auStack_214;
  }
  else {
    local_220 = param_1;
    puStack_21c = param_2;
    if (param_2 == (undefined1 *)0x0) goto LAB_000155f2;
    puVar1 = param_1;
    if ((uint)param_2 >> 9 == 0) {
      puVar1 = auStack_214;
    }
  }
  stbsp_vsprintfcb(DAT_00015638 + 0x15604,&local_220,puVar1,param_3,param_4);
  puVar1 = local_220 + -(int)param_1;
  if (param_2 <= puVar1) {
    puVar1 = param_2 + -1;
  }
  param_1[(int)puVar1] = 0;
LAB_00015614:
  if (**(int **)(DAT_0001563c + 0x1561e) != local_14) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail(local_218);
  }
  return;
}


// ==== FUN_0001564c @ 0001564c

undefined4 * FUN_0001564c(undefined8 *param_1,undefined4 *param_2,uint param_3)

{
  uint uVar1;
  undefined8 *puVar2;
  undefined8 *puVar3;
  uint uVar4;
  uint uVar5;
  undefined8 *puVar6;
  undefined8 *puVar7;
  undefined8 uVar8;
  undefined8 uVar9;
  
  uVar1 = param_2[1];
  param_2[2] = param_2[2] + param_3;
  if ((int)uVar1 <= (int)param_3) {
    param_3 = uVar1;
  }
  if (param_3 == 0) goto LAB_000156c4;
  puVar7 = (undefined8 *)*param_2;
  if (param_1 != puVar7) {
    puVar6 = (undefined8 *)((int)param_1 + param_3);
    puVar2 = (undefined8 *)((int)param_1 + 1);
    if ((undefined8 *)((int)param_1 + 1) < puVar6) {
      puVar2 = puVar6;
    }
    uVar1 = (int)puVar2 - (int)param_1;
    if (0xf < uVar1) {
      puVar2 = (undefined8 *)((int)puVar7 - (int)param_1);
    }
    if (0xf < uVar1 && (undefined8 *)0xf < puVar2) {
      uVar5 = uVar1 & 0xfffffff0;
      puVar3 = (undefined8 *)((int)puVar7 + uVar5);
      puVar2 = (undefined8 *)((int)param_1 + uVar5);
      uVar4 = uVar5;
      do {
        uVar8 = *param_1;
        uVar9 = param_1[1];
        param_1 = param_1 + 2;
        uVar4 = uVar4 - 0x10;
        *puVar7 = uVar8;
        puVar7[1] = uVar9;
        puVar7 = puVar7 + 2;
      } while (uVar4 != 0);
      if (uVar1 != uVar5) goto LAB_000156aa;
    }
    else {
      puVar2 = param_1;
      puVar3 = puVar7;
LAB_000156aa:
      do {
        puVar7 = (undefined8 *)((int)puVar2 + 1);
        *(undefined1 *)puVar3 = *(undefined1 *)puVar2;
        puVar2 = puVar7;
        puVar3 = (undefined8 *)((int)puVar3 + 1);
      } while (puVar7 < puVar6);
    }
    puVar7 = (undefined8 *)*param_2;
    uVar1 = param_2[1];
  }
  uVar1 = uVar1 - param_3;
  *param_2 = (undefined1 *)((int)puVar7 + param_3);
  param_2[1] = uVar1;
LAB_000156c4:
  if (((int)uVar1 < 1) || (uVar1 < 0x200)) {
    return param_2 + 3;
  }
  return (undefined4 *)*param_2;
}


// ==== stbsp_snprintf @ 000156e0

void stbsp_snprintf(undefined1 *param_1,undefined1 *param_2,undefined4 param_3,undefined4 param_4)

{
  undefined1 *puVar1;
  undefined1 *local_224;
  undefined1 *puStack_220;
  undefined4 local_21c;
  undefined1 auStack_218 [512];
  int local_18;
  undefined4 local_4;
  
  local_18 = **(int **)(DAT_0001578c + 0x156fc);
  local_4 = param_4;
  if (param_1 == (undefined1 *)0x0 && param_2 == (undefined1 *)0x0) {
    local_21c = 0;
    stbsp_vsprintfcb(DAT_00015790 + 0x1573a,&local_224,auStack_218,param_3,&local_4);
    goto LAB_0001576a;
  }
  local_21c = 0;
  if ((int)param_2 < 0) {
    puStack_220 = (undefined1 *)0x0;
    local_224 = param_2;
LAB_00015748:
    puVar1 = auStack_218;
  }
  else {
    local_224 = param_1;
    puStack_220 = param_2;
    if (param_2 == (undefined1 *)0x0) goto LAB_00015748;
    puVar1 = param_1;
    if ((uint)param_2 >> 9 == 0) {
      puVar1 = auStack_218;
    }
  }
  stbsp_vsprintfcb(DAT_00015794 + 0x1575a,&local_224,puVar1,param_3,&local_4);
  puVar1 = local_224 + -(int)param_1;
  if (param_2 <= puVar1) {
    puVar1 = param_2 + -1;
  }
  param_1[(int)puVar1] = 0;
LAB_0001576a:
  if (**(int **)(DAT_00015798 + 0x15774) != local_18) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail(local_21c);
  }
  return;
}


// ==== stbsp_vsprintf @ 0001579c

void stbsp_vsprintf(undefined4 param_1,undefined4 param_2,undefined4 param_3)

{
  stbsp_vsprintfcb(0,0,param_1,param_2,param_3);
  return;
}


// ==== FUN_000157b8 @ 000157b8

void FUN_000157b8(char *param_1,char *param_2)

{
  if (*(code **)(param_1 + 0x24) != (code *)0x0) {
    (**(code **)(param_1 + 0x24))(param_1);
  }
  strncpy(param_1,param_2,0x1e);
  param_1[0x1e] = '\0';
  return;
}


// ==== _INIT_3 @ 000157d8

void _INIT_3(void)

{
  int iVar1;
  
  iVar1 = DAT_00015804;
  __strncpy_chk2(DAT_00015804 + 0x157ea,DAT_00015808 + 0x157ee,0x1f,0x1f,8);
  *(undefined4 *)((int)&DAT_00015808 + iVar1 + 2) = 0;
  *(undefined4 *)(ModInfo::ModInfo + iVar1 + 2) = 0;
  *(undefined4 *)(ModInfo::ModInfo + iVar1 + 6) = 0;
  *(undefined1 *)((int)&DAT_00015808 + iVar1 + 1) = 1;
  return;
}


// ==== ModInfo @ 0001580c

/* ModInfo::ModInfo(char const*, char const*, char const*, char const*) */

ModInfo * __thiscall
ModInfo::ModInfo(ModInfo *this,char *param_1,char *param_2,char *param_3,char *param_4)

{
  ModInfo *param0;
  ModInfo MVar1;
  ModInfo MVar2;
  ModInfo *pMVar3;
  ModInfo *pMVar4;
  int iVar5;
  ModInfo *param4;
  
  iVar5 = 0;
  do {
    if (iVar5 == 0x2f) {
      this[0x2f] = (ModInfo)0x0;
      break;
    }
    MVar2 = *(ModInfo *)(param_1 + iVar5);
    this[iVar5] = MVar2;
    iVar5 = iVar5 + 1;
  } while (MVar2 != (ModInfo)0x0);
  this[0x2f] = (ModInfo)0x0;
  iVar5 = 0;
  do {
    if (iVar5 == 0x2f) {
      this[0x5f] = (ModInfo)0x0;
      break;
    }
    MVar2 = *(ModInfo *)(param_2 + iVar5);
    this[iVar5 + 0x30] = MVar2;
    iVar5 = iVar5 + 1;
  } while (MVar2 != (ModInfo)0x0);
  this[0x5f] = (ModInfo)0x0;
  MVar2 = (ModInfo)*param_3;
  param0 = this + 0x60;
  *param0 = MVar2;
  MVar1 = (ModInfo)0x0;
  if (MVar2 != (ModInfo)0x0) {
    MVar1 = *(ModInfo *)(param_3 + 1);
    this[0x61] = MVar1;
  }
  if (MVar2 != (ModInfo)0x0 && MVar1 != (ModInfo)0x0) {
    MVar2 = *(ModInfo *)(param_3 + 2);
    this[0x62] = MVar2;
    MVar1 = (ModInfo)0x0;
    if (MVar2 != (ModInfo)0x0) {
      MVar1 = *(ModInfo *)(param_3 + 3);
      this[99] = MVar1;
    }
    if (MVar2 != (ModInfo)0x0 && MVar1 != (ModInfo)0x0) {
      MVar2 = *(ModInfo *)(param_3 + 4);
      this[100] = MVar2;
      MVar1 = (ModInfo)0x0;
      if (MVar2 != (ModInfo)0x0) {
        MVar1 = *(ModInfo *)(param_3 + 5);
        this[0x65] = MVar1;
      }
      if (MVar2 != (ModInfo)0x0 && MVar1 != (ModInfo)0x0) {
        MVar2 = *(ModInfo *)(param_3 + 6);
        this[0x66] = MVar2;
        MVar1 = (ModInfo)0x0;
        if (MVar2 != (ModInfo)0x0) {
          MVar1 = *(ModInfo *)(param_3 + 7);
          this[0x67] = MVar1;
        }
        if (MVar2 != (ModInfo)0x0 && MVar1 != (ModInfo)0x0) {
          MVar2 = *(ModInfo *)(param_3 + 8);
          this[0x68] = MVar2;
          MVar1 = (ModInfo)0x0;
          if (MVar2 != (ModInfo)0x0) {
            MVar1 = *(ModInfo *)(param_3 + 9);
            this[0x69] = MVar1;
          }
          if (MVar2 != (ModInfo)0x0 && MVar1 != (ModInfo)0x0) {
            MVar2 = *(ModInfo *)(param_3 + 10);
            this[0x6a] = MVar2;
            MVar1 = (ModInfo)0x0;
            if (MVar2 != (ModInfo)0x0) {
              MVar1 = *(ModInfo *)(param_3 + 0xb);
              this[0x6b] = MVar1;
            }
            if (MVar2 != (ModInfo)0x0 && MVar1 != (ModInfo)0x0) {
              MVar2 = *(ModInfo *)(param_3 + 0xc);
              this[0x6c] = MVar2;
              MVar1 = (ModInfo)0x0;
              if (MVar2 != (ModInfo)0x0) {
                MVar1 = *(ModInfo *)(param_3 + 0xd);
                this[0x6d] = MVar1;
              }
              if (MVar2 != (ModInfo)0x0 && MVar1 != (ModInfo)0x0) {
                MVar2 = *(ModInfo *)(param_3 + 0xe);
                this[0x6e] = MVar2;
                MVar1 = (ModInfo)0x0;
                if (MVar2 != (ModInfo)0x0) {
                  MVar1 = *(ModInfo *)(param_3 + 0xf);
                  this[0x6f] = MVar1;
                }
                if (MVar2 != (ModInfo)0x0 && MVar1 != (ModInfo)0x0) {
                  MVar2 = *(ModInfo *)(param_3 + 0x10);
                  this[0x70] = MVar2;
                  MVar1 = (ModInfo)0x0;
                  if (MVar2 != (ModInfo)0x0) {
                    MVar1 = *(ModInfo *)(param_3 + 0x11);
                    this[0x71] = MVar1;
                  }
                  if (MVar2 != (ModInfo)0x0 && MVar1 != (ModInfo)0x0) {
                    MVar2 = *(ModInfo *)(param_3 + 0x12);
                    this[0x72] = MVar2;
                    MVar1 = (ModInfo)0x0;
                    if (MVar2 != (ModInfo)0x0) {
                      MVar1 = *(ModInfo *)(param_3 + 0x13);
                      this[0x73] = MVar1;
                    }
                    if (MVar2 != (ModInfo)0x0 && MVar1 != (ModInfo)0x0) {
                      MVar2 = *(ModInfo *)(param_3 + 0x14);
                      this[0x74] = MVar2;
                      MVar1 = (ModInfo)0x0;
                      if (MVar2 != (ModInfo)0x0) {
                        MVar1 = *(ModInfo *)(param_3 + 0x15);
                        this[0x75] = MVar1;
                      }
                      if ((MVar2 != (ModInfo)0x0 && MVar1 != (ModInfo)0x0) &&
                         (MVar2 = *(ModInfo *)(param_3 + 0x16), this[0x76] = MVar2,
                         MVar2 != (ModInfo)0x0)) {
                        this[0x77] = (ModInfo)0x0;
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  this[0x77] = (ModInfo)0x0;
  iVar5 = 0;
  do {
    if (iVar5 == 0x2f) {
      this[0xa7] = (ModInfo)0x0;
      break;
    }
    MVar2 = *(ModInfo *)(param_4 + iVar5);
    this[iVar5 + 0x78] = MVar2;
    iVar5 = iVar5 + 1;
  } while (MVar2 != (ModInfo)0x0);
  this[0xa7] = (ModInfo)0x0;
  pMVar4 = this;
  MVar2 = *this;
  while ((byte)MVar2 != 0) {
    pMVar3 = pMVar4 + 1;
    if (0xffffffe5 < (byte)MVar2 - 0x5b) {
      MVar2 = (ModInfo)((byte)MVar2 | 0x20);
    }
    *pMVar4 = MVar2;
    pMVar4 = pMVar3;
    MVar2 = *pMVar3;
  }
  pMVar3 = this + 0xa8;
  pMVar4 = this + 0xaa;
  param4 = this + 0xac;
  iVar5 = sscanf((char *)param0,(char *)(DAT_000159fc + 0x159a0),(ushort *)pMVar3,(ushort *)pMVar4,
                 (ushort *)param4,(ushort *)(this + 0xae));
  if (iVar5 < 4) {
    iVar5 = sscanf((char *)param0,(char *)(DAT_00015a00 + 0x159c0),(ushort *)pMVar3,(ushort *)pMVar4
                   ,(ushort *)param4);
    if (iVar5 < 3) {
      iVar5 = sscanf((char *)param0,(char *)(DAT_00015a04 + 0x159d6),(ushort *)pMVar3,
                     (ushort *)pMVar4);
      if (iVar5 < 2) {
        iVar5 = atoi((char *)param0);
        *(short *)pMVar3 = (short)iVar5;
      }
      *(undefined2 *)param4 = 0;
    }
    *(undefined2 *)(this + 0xae) = 0;
  }
  return this;
}


// ==== __GetModInfo @ 00015a08

undefined4 __GetModInfo(void)

{
  return **(undefined4 **)(__GetModInfo + DAT_00015a14 + 6);
}


// ==== FUN_00015a18 @ 00015a18

void FUN_00015a18(int param_1)

{
  int iVar1;
  int *piVar2;
  undefined4 *puVar3;
  
  piVar2 = (int *)(DAT_00015b58 + 0x15a34);
  iVar1 = *piVar2 + 0x2ac516;
  puVar3 = (undefined4 *)(DAT_00015b5c + 0x15a54);
  if (param_1 == 0) {
    (**(code **)(*(int *)*puVar3 + 0x30))((int *)*puVar3,iVar1,*piVar2 + 0x2ac57c);
    (**(code **)(*(int *)*puVar3 + 0xb0))((int *)*puVar3,*piVar2 + 0x2b0bd8,1);
    (**(code **)(*(int *)*puVar3 + 0x30))((int *)*puVar3,*piVar2 + 0x2b41b2,*piVar2 + 0x2b4314);
    (**(code **)(*(int *)*puVar3 + 0x30))((int *)*puVar3,*piVar2 + 0x2b485e,*piVar2 + 0x2b4b54);
    (**(code **)(*(int *)*puVar3 + 0x30))((int *)*puVar3,*piVar2 + 0x2c17a6,*piVar2 + 0x2c1856);
                    /* WARNING: Could not recover jumptable at 0x00015b54. Too many branches */
                    /* WARNING: Treating indirect jump as call */
    (**(code **)(*(int *)*puVar3 + 0x30))((int *)*puVar3,*piVar2 + 0x2c3050,*piVar2 + 0x2c3108);
    return;
  }
  (**(code **)(*(int *)*puVar3 + 0x24))((int *)*puVar3,iVar1,DAT_00015b60 + 0x15a68,4);
  (**(code **)(*(int *)*puVar3 + 0x24))((int *)*puVar3,*piVar2 + 0x2b0bd8,DAT_00015b64 + 0x15a7a,4);
  (**(code **)(*(int *)*puVar3 + 0x24))((int *)*puVar3,*piVar2 + 0x2b41b2,DAT_00015b68 + 0x15a8e,4);
  (**(code **)(*(int *)*puVar3 + 0x24))((int *)*puVar3,*piVar2 + 0x2b485e,DAT_00015b6c + 0x15aa2,4);
  (**(code **)(*(int *)*puVar3 + 0x24))((int *)*puVar3,*piVar2 + 0x2c17a6,DAT_00015b70 + 0x15ab6,4);
                    /* WARNING: Could not recover jumptable at 0x00015adc. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (**(code **)(*(int *)*puVar3 + 0x24))((int *)*puVar3,*piVar2 + 0x2c3050,DAT_00015b74 + 0x15ad0,4);
  return;
}


// ==== FUN_00015c84 @ 00015c84

void FUN_00015c84(undefined4 param_1,undefined4 param_2,int param_3)

{
  byte bVar1;
  undefined4 *puVar2;
  
  puVar2 = (undefined4 *)(DAT_00015ca8 + 0x15c94);
  bVar1 = *(byte *)(DAT_00015ca4 + 0x15c90);
  if (*(byte *)(param_3 + 3) < *(byte *)(DAT_00015ca4 + 0x15c90)) {
    bVar1 = *(byte *)(param_3 + 3);
  }
  *(byte *)(param_3 + 3) = bVar1;
                    /* WARNING: Could not recover jumptable at 0x00015ca2. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*(code *)*puVar2)();
  return;
}


// ==== FUN_00015cac @ 00015cac

void FUN_00015cac(undefined4 param_1,uint param_2,undefined4 param_3)

{
  undefined1 uVar1;
  int *piVar2;
  int iVar3;
  uint uVar4;
  int iVar5;
  code *UNRECOVERED_JUMPTABLE;
  undefined4 *puVar6;
  uint *puVar7;
  int iVar8;
  
  uVar4 = param_2;
  switch(param_3) {
  case 0:
    puVar6 = (undefined4 *)(DAT_00015fc8 + 0x15cdc);
    (**(code **)(*(int *)*puVar6 + 0xd0))((int *)*puVar6,DAT_00015fcc + 0x15ce8,param_2);
    if (param_2 == 2) {
      (**(code **)(*(int *)*puVar6 + 0x34))
                ((int *)*puVar6,*(int *)(DAT_00015fe4 + 0x15fae) + 0x437aca);
      uVar1 = 1;
    }
    else if (param_2 == 1) {
      piVar2 = (int *)(DAT_00015fdc + 0x15f66);
      (**(code **)(*(int *)*puVar6 + 0x24))
                ((int *)*puVar6,*piVar2 + 0x437aca,DAT_00015fe0 + 0x15f78,4);
      iVar3 = *piVar2;
      (**(code **)(*(int *)*puVar6 + 0x30))((int *)*puVar6,iVar3 + 0x4434ce,iVar3 + 0x443770);
      uVar1 = 0;
    }
    else {
      if (param_2 != 0) {
        return;
      }
      piVar2 = (int *)(DAT_00015fd0 + 0x15d06);
      (**(code **)(*(int *)*puVar6 + 0x24))
                ((int *)*puVar6,*piVar2 + 0x437aca,DAT_00015fd4 + 0x15d18,4);
      (**(code **)(*(int *)*puVar6 + 0x24))
                ((int *)*puVar6,*piVar2 + 0x4434ce,DAT_00015fd8 + 0x15d32,4);
      uVar1 = 0;
    }
    **(undefined1 **)(DAT_00015fe8 + 0x15fbe) = uVar1;
    return;
  case 1:
    puVar6 = (undefined4 *)(DAT_00015fec + 0x15e1c);
    (**(code **)(*(int *)*puVar6 + 0xd0))((int *)*puVar6,DAT_00015ff0 + 0x15e2c,param_2 != 0);
    iVar3 = *(int *)(DAT_00015ff4 + 0x15e34) + 0x2bd044;
    if (param_2 == 0) {
      (**(code **)(*(int *)*puVar6 + 0x30))
                ((int *)*puVar6,iVar3,*(int *)(DAT_00015ff4 + 0x15e34) + 0x2bd4da);
    }
    else {
      (**(code **)(*(int *)*puVar6 + 0x24))((int *)*puVar6,iVar3,DAT_00015ff8 + 0x15e50,4);
    }
    *(bool *)(DAT_00015ffc + 0x15f46) = param_2 != 0;
    return;
  case 2:
    puVar6 = (undefined4 *)(DAT_00016000 + 0x15d7e);
    (**(code **)(*(int *)*puVar6 + 0xd0))((int *)*puVar6,DAT_00016004 + 0x15d8e,param_2 != 0);
    iVar3 = *(int *)(DAT_00016008 + 0x15d96) + 0x2ab458;
    if (param_2 != 0) {
      piVar2 = (int *)*puVar6;
      iVar5 = *piVar2;
      iVar8 = DAT_0001600c + 0x15db2;
LAB_00015eae:
                    /* WARNING: Could not recover jumptable at 0x00015ebc. Too many branches */
                    /* WARNING: Treating indirect jump as call */
      (**(code **)(iVar5 + 0x24))(piVar2,iVar3,iVar8,4);
      return;
    }
    piVar2 = (int *)*puVar6;
    uVar4 = *(int *)(DAT_00016008 + 0x15d96) + 0x2aba70;
    goto LAB_00015f4e;
  case 3:
    puVar6 = (undefined4 *)(DAT_00016010 + 0x15dbc);
    piVar2 = (int *)*puVar6;
    (**(code **)(*piVar2 + 0xd0))(piVar2,DAT_00016014 + 0x15dcc,param_2 != 0);
    piVar2 = (int *)*puVar6;
    iVar5 = *piVar2;
    iVar3 = *(int *)(DAT_00016018 + 0x15ddc) + 0x2ab346;
    if (param_2 != 0) {
      iVar8 = DAT_0001601c + 0x15dec;
      goto LAB_00015eae;
    }
    UNRECOVERED_JUMPTABLE = *(code **)(iVar5 + 0xb0);
    uVar4 = 1;
    break;
  case 4:
    (**(code **)(**(int **)(DAT_00016020 + 0x15d42) + 0xd0))
              (*(int **)(DAT_00016020 + 0x15d42),DAT_00016024 + 0x15d54,param_2 != 0);
    FUN_00015a18(param_2 != 0);
    return;
  case 5:
    (**(code **)(**(int **)(DAT_00016068 + 0x15e5a) + 0xd0))
              (*(int **)(DAT_00016068 + 0x15e5a),DAT_0001606c + 0x15e6c,param_2 != 0);
    *(bool *)(DAT_00016070 + 0x15e72) = param_2 != 0;
    return;
  case 6:
    puVar6 = (undefined4 *)(DAT_00016028 + 0x15e82);
    (**(code **)(*(int *)*puVar6 + 0xd0))((int *)*puVar6,DAT_0001602c + 0x15e92,param_2 != 0);
    iVar3 = *(int *)(DAT_00016030 + 0x15ea2) + 0x443f22;
    if (param_2 != 0) {
      piVar2 = (int *)*puVar6;
      iVar5 = *piVar2;
      iVar8 = DAT_00016034 + 0x15eb0;
      goto LAB_00015eae;
    }
    piVar2 = (int *)*puVar6;
    uVar4 = *(int *)(DAT_00016030 + 0x15ea2) + 0x4440ca;
LAB_00015f4e:
    UNRECOVERED_JUMPTABLE = *(code **)(*piVar2 + 0x30);
    break;
  case 7:
    piVar2 = *(int **)(DAT_00016044 + 0x15df6);
    if (0xfe < param_2) {
      uVar4 = 0xff;
    }
    UNRECOVERED_JUMPTABLE = *(code **)(*piVar2 + 0xd0);
    if ((int)param_2 < 0) {
      uVar4 = 0;
    }
    iVar3 = DAT_0001604c + 0x15e10;
    *(uint *)(DAT_00016048 + 0x15dfe) = uVar4;
    break;
  case 8:
    piVar2 = *(int **)(DAT_00016038 + 0x15ef2);
    if (0xfe < param_2) {
      uVar4 = 0xff;
    }
    UNRECOVERED_JUMPTABLE = *(code **)(*piVar2 + 0xd0);
    if ((int)param_2 < 0) {
      uVar4 = 0;
    }
    iVar3 = DAT_00016040 + 0x15f0c;
    *(uint *)(DAT_0001603c + 0x15efa) = uVar4;
    break;
  case 9:
    piVar2 = *(int **)(DAT_00016050 + 0x15d68);
    iVar8 = *piVar2;
    puVar7 = (uint *)(DAT_00016054 + 0x15d70);
    iVar3 = DAT_00016058 + 0x15d74;
    goto LAB_00015ece;
  case 10:
    piVar2 = *(int **)(DAT_0001605c + 0x15ec4);
    iVar8 = *piVar2;
    puVar7 = (uint *)(DAT_00016060 + 0x15ecc);
    iVar3 = DAT_00016064 + 0x15ed0;
LAB_00015ece:
    if (499 < param_2) {
      uVar4 = 500;
    }
    UNRECOVERED_JUMPTABLE = *(code **)(iVar8 + 0xd0);
    if ((int)param_2 < 100) {
      uVar4 = 100;
    }
    *puVar7 = uVar4;
    break;
  default:
    return;
  }
                    /* WARNING: Could not recover jumptable at 0x00015f5a. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*UNRECOVERED_JUMPTABLE)(piVar2,iVar3,uVar4);
  return;
}


// ==== OnModLoad @ 00016164

void OnModLoad(void)

{
  char cVar1;
  undefined1 uVar2;
  undefined4 uVar3;
  uint uVar4;
  char *pcVar5;
  char *pcVar6;
  char *pcVar7;
  undefined1 *puVar8;
  char *pcVar9;
  char *pcVar10;
  int *piVar11;
  undefined1 *puVar12;
  int iVar13;
  code *pcVar14;
  uint uVar15;
  int *piVar16;
  uint *puVar17;
  int iVar18;
  uint *puVar19;
  uint *puVar20;
  uint *puVar21;
  undefined4 *puVar22;
  uint *puVar23;
  int local_2c;
  int local_28;
  
  local_28 = **(int **)(DAT_000168b8 + 0x16180);
  FUN_000157b8(*(undefined4 *)(DAT_000164c0 + 0x16176),DAT_000164c4 + 0x16178);
  if (*(char *)(DAT_000164c8 + 0x1618e) != '\x01') goto LAB_0001689c;
  FUN_0001819c();
  puVar22 = (undefined4 *)(DAT_000164cc + 0x161a0);
  piVar16 = (int *)*puVar22;
  uVar3 = FUN_000126ac(DAT_000164d0 + 0x161a2);
  (**(code **)(*piVar16 + 0x18))(piVar16,uVar3,DAT_000164d4 + 0x161b4,DAT_000164d8 + 0x161b6);
  piVar16 = (int *)(DAT_000164dc + 0x161ca);
  iVar13 = DAT_000164e0 + 0x161d4;
  iVar18 = DAT_000164e4 + 0x161de;
  (**(code **)(*(int *)*puVar22 + 0xc0))((int *)*puVar22,*piVar16 + 0x2c1803,iVar13,iVar18);
  (**(code **)(*(int *)*puVar22 + 0xc0))((int *)*puVar22,*piVar16 + 0x2c1853,iVar13,iVar18);
  (**(code **)(*(int *)*puVar22 + 0xc0))((int *)*puVar22,*piVar16 + 0x2c30b5,iVar13,iVar18);
  (**(code **)(*(int *)*puVar22 + 0xc0))((int *)*puVar22,*piVar16 + 0x2c3105,iVar13,iVar18);
  (**(code **)(*(int *)*puVar22 + 0xc0))((int *)*puVar22,*piVar16 + 0x2ac573,iVar13,iVar18);
  puVar19 = (uint *)(DAT_000164e8 + 0x1624e);
  (**(code **)(*(int *)*puVar22 + 0xe0))((int *)*puVar22,DAT_000164ec + 0x16254,puVar19);
  if ((int)*puVar19 < 0) {
    uVar4 = 0;
LAB_00016266:
    *puVar19 = uVar4;
  }
  else if (3 < *puVar19) {
    uVar4 = 3;
    goto LAB_00016266;
  }
  local_2c = 0;
  iVar13 = (**(code **)(*(int *)*puVar22 + 0xe0))((int *)*puVar22,DAT_000164f0 + 0x1627a,&local_2c);
  pcVar5 = (char *)(DAT_000164f4 + 0x16284);
  if (iVar13 != 0) {
    *pcVar5 = local_2c != 0;
  }
  local_2c = 0;
  iVar13 = (**(code **)(*(int *)*puVar22 + 0xe0))((int *)*puVar22,DAT_000164f8 + 0x162a2,&local_2c);
  pcVar6 = (char *)(DAT_000164fc + 0x162ac);
  if (iVar13 != 0) {
    *pcVar6 = local_2c != 0;
  }
  local_2c = 0;
  iVar13 = (**(code **)(*(int *)*puVar22 + 0xe0))((int *)*puVar22,DAT_00016500 + 0x162ca,&local_2c);
  pcVar7 = (char *)(DAT_00016504 + 0x162d4);
  if (iVar13 != 0) {
    *pcVar7 = local_2c != 0;
  }
  local_2c = 0;
  iVar13 = (**(code **)(*(int *)*puVar22 + 0xe0))((int *)*puVar22,DAT_00016508 + 0x162f2,&local_2c);
  puVar8 = (undefined1 *)(DAT_0001650c + 0x162fc);
  if (iVar13 != 0) {
    *puVar8 = local_2c != 0;
  }
  local_2c = 0;
  iVar13 = (**(code **)(*(int *)*puVar22 + 0xe0))((int *)*puVar22,DAT_00016510 + 0x1631c,&local_2c);
  pcVar9 = (char *)(DAT_00016514 + 0x16326);
  pcVar10 = pcVar9;
  if (iVar13 != 0) {
    pcVar10 = (char *)(uint)(local_2c != 0);
    *pcVar9 = local_2c != 0;
  }
  FUN_00015cac(pcVar10,*puVar19,0);
  cVar1 = *pcVar5;
  (**(code **)(*(int *)*puVar22 + 0xd0))((int *)*puVar22,DAT_00016518 + 0x1634a,cVar1);
  iVar13 = *piVar16 + 0x2ab458;
  if (cVar1 == '\0') {
    (**(code **)(*(int *)*puVar22 + 0x30))((int *)*puVar22,iVar13,*piVar16 + 0x2aba70);
  }
  else {
    (**(code **)(*(int *)*puVar22 + 0x24))((int *)*puVar22,iVar13,DAT_0001651c + 0x16376,4);
  }
  cVar1 = *pcVar6;
  (**(code **)(*(int *)*puVar22 + 0xd0))((int *)*puVar22,DAT_00016520 + 0x1639c,cVar1);
  piVar11 = (int *)*puVar22;
  if (cVar1 == '\0') {
    (**(code **)(*piVar11 + 0xb0))(piVar11,*piVar16 + 0x2ab346,1);
  }
  else {
    (**(code **)(*piVar11 + 0x24))(piVar11,*piVar16 + 0x2ab346,DAT_00016524 + 0x163b6,4);
  }
  cVar1 = *pcVar7;
  (**(code **)(*(int *)*puVar22 + 0xd0))((int *)*puVar22,DAT_00016528 + 0x163ce,cVar1);
  iVar13 = *piVar16 + 0x2bd044;
  if (cVar1 == '\0') {
    (**(code **)(*(int *)*puVar22 + 0x30))((int *)*puVar22,iVar13,*piVar16 + 0x2bd4da);
  }
  else {
    (**(code **)(*(int *)*puVar22 + 0x24))((int *)*puVar22,iVar13,DAT_0001652c + 0x163f6,4);
  }
  piVar11 = (int *)*puVar22;
  pcVar14 = *(code **)(*piVar11 + 0xd0);
  iVar13 = DAT_00016534 + 0x1641e;
  *(char *)(DAT_00016530 + 0x16418) = cVar1;
  uVar2 = *puVar8;
  (*pcVar14)(piVar11,iVar13,uVar2);
  FUN_00015a18(uVar2);
  cVar1 = *pcVar9;
  (**(code **)(*(int *)*puVar22 + 0xd0))((int *)*puVar22,DAT_00016538 + 0x16436,cVar1);
  iVar13 = *piVar16 + 0x443f22;
  if (cVar1 == '\0') {
    (**(code **)(*(int *)*puVar22 + 0x30))((int *)*puVar22,iVar13,*piVar16 + 0x4440ca);
  }
  else {
    (**(code **)(*(int *)*puVar22 + 0x24))((int *)*puVar22,iVar13,DAT_0001653c + 0x16460,4);
  }
  puVar20 = (uint *)(DAT_00016540 + 0x1647c);
  (**(code **)(*(int *)*puVar22 + 0xe0))((int *)*puVar22,DAT_00016544 + 0x16482,puVar20);
  if ((int)*puVar20 < 0) {
    uVar4 = 0;
LAB_00016496:
    *puVar20 = uVar4;
  }
  else if (0xff < *puVar20) {
    uVar4 = 0xff;
    goto LAB_00016496;
  }
  puVar23 = (uint *)(DAT_00016548 + 0x164a4);
  (**(code **)(*(int *)*puVar22 + 0xe0))((int *)*puVar22,DAT_0001654c + 0x164aa,puVar23);
  if ((int)*puVar23 < 0) {
    uVar4 = 0;
LAB_00016552:
    *puVar23 = uVar4;
  }
  else if (0xff < *puVar23) {
    uVar4 = 0xff;
    goto LAB_00016552;
  }
  local_2c = 0;
  iVar13 = (**(code **)(*(int *)*puVar22 + 0xe0))((int *)*puVar22,DAT_000168bc + 0x16568,&local_2c);
  puVar12 = (undefined1 *)(DAT_000168c0 + 0x16570);
  if (iVar13 != 0) {
    *puVar12 = local_2c != 0;
  }
  piVar16 = (int *)*puVar22;
  uVar15 = *puVar20;
  pcVar14 = *(code **)(*piVar16 + 0xd0);
  iVar13 = DAT_000168c4 + 0x16594;
  uVar4 = 0xff;
  if (uVar15 < 0xff) {
    uVar4 = uVar15;
  }
  if ((int)uVar15 < 0) {
    uVar4 = 0;
  }
  *puVar20 = uVar4;
  (*pcVar14)(piVar16,iVar13);
  piVar16 = (int *)*puVar22;
  uVar15 = *puVar23;
  uVar4 = 0xff;
  if (uVar15 < 0xff) {
    uVar4 = uVar15;
  }
  pcVar14 = *(code **)(*piVar16 + 0xd0);
  iVar13 = DAT_000168c8 + 0x165bc;
  if ((int)uVar15 < 0) {
    uVar4 = 0;
  }
  *puVar23 = uVar4;
  (*pcVar14)(piVar16,iVar13,uVar4);
  uVar2 = *puVar12;
  (**(code **)(*(int *)*puVar22 + 0xd0))((int *)*puVar22,DAT_000168cc + 0x165da,uVar2);
  piVar16 = (int *)*puVar22;
  puVar17 = (uint *)(DAT_000168d0 + 0x165ea);
  pcVar14 = *(code **)(*piVar16 + 0xe0);
  iVar13 = DAT_000168d4 + 0x165f0;
  *puVar12 = uVar2;
  (*pcVar14)(piVar16,iVar13,puVar17);
  if ((int)*puVar17 < 100) {
    uVar4 = 100;
LAB_0001660e:
    *puVar17 = uVar4;
  }
  else if (500 < *puVar17) {
    uVar4 = 500;
    goto LAB_0001660e;
  }
  puVar21 = (uint *)(DAT_000168d8 + 0x1661e);
  (**(code **)(*(int *)*puVar22 + 0xe0))((int *)*puVar22,DAT_000168dc + 0x16624,puVar21);
  if ((int)*puVar21 < 100) {
    uVar4 = 100;
LAB_0001663a:
    *puVar21 = uVar4;
  }
  else if (500 < *puVar21) {
    uVar4 = 500;
    goto LAB_0001663a;
  }
  piVar16 = (int *)*puVar22;
  uVar15 = *puVar17;
  pcVar14 = *(code **)(*piVar16 + 0xd0);
  iVar13 = DAT_000168e0 + 0x1665c;
  uVar4 = 500;
  if (uVar15 < 500) {
    uVar4 = uVar15;
  }
  if ((int)uVar15 < 100) {
    uVar4 = 100;
  }
  *puVar17 = uVar4;
  (*pcVar14)(piVar16,iVar13);
  piVar16 = (int *)*puVar22;
  uVar15 = *puVar21;
  uVar4 = 500;
  if (uVar15 < 500) {
    uVar4 = uVar15;
  }
  pcVar14 = *(code **)(*piVar16 + 0xd0);
  iVar13 = DAT_000168e4 + 0x16688;
  if ((int)uVar15 < 100) {
    uVar4 = 100;
  }
  *puVar21 = uVar4;
  (*pcVar14)(piVar16,iVar13,uVar4);
  uVar3 = dlopen(DAT_000168e8 + 0x1669a,0);
  pcVar14 = (code *)dlsym(uVar3,DAT_000168ec + 0x166a2);
  iVar13 = (*pcVar14)(DAT_000168f0 + 0x166ac);
  piVar16 = *(int **)(DAT_000168f4 + 0x166b4);
  *piVar16 = iVar13;
  if ((iVar13 != 0) &&
     (iVar13 = (**(code **)(*(int *)*puVar22 + 0xa8))
                         ((int *)*puVar22,DAT_000168f8 + 0x166cc,DAT_000168fc + 0x166ce),
     iVar13 != 0)) {
    uVar3 = (**(code **)(*(int *)*piVar16 + 0x24))
                      ((int *)*piVar16,DAT_00016900 + 0x166e2,DAT_00016904 + 0x166e4);
    (**(code **)(*(int *)*piVar16 + 0x14))
              ((int *)*piVar16,uVar3,DAT_00016908 + 0x166f4,DAT_0001690c + 0x166f6);
    iVar18 = DAT_00016910 + 0x16716;
    iVar13 = DAT_00016914 + 0x1671c;
    (**(code **)(*(int *)*piVar16 + 0x94))
              ((int *)*piVar16,uVar3,DAT_00016918 + 0x16732,*pcVar5,0,1,iVar13,iVar18,2);
    (**(code **)(*(int *)*piVar16 + 0x94))
              ((int *)*piVar16,uVar3,DAT_0001691c + 0x16756,*pcVar6,0,1,iVar13,iVar18,3);
    (**(code **)(*(int *)*piVar16 + 0x94))
              ((int *)*piVar16,uVar3,DAT_00016924 + 0x16778,*puVar19,0,3,DAT_00016920 + 0x1676e,
               iVar18,0);
    (**(code **)(*(int *)*piVar16 + 0x94))
              ((int *)*piVar16,uVar3,DAT_00016928 + 0x1679e,*pcVar7,0,1,iVar13,iVar18,1);
    (**(code **)(*(int *)*piVar16 + 0x94))
              ((int *)*piVar16,uVar3,DAT_0001692c + 0x167be,*puVar8,0,1,iVar13,iVar18,4);
    (**(code **)(*(int *)*piVar16 + 0x98))
              ((int *)*piVar16,uVar3,DAT_00016930 + 0x167e2,*puVar20,0,0xff,iVar18,0,8);
    (**(code **)(*(int *)*piVar16 + 0x98))
              ((int *)*piVar16,uVar3,DAT_00016934 + 0x16804,*puVar23,0,0xff,iVar18,0,7);
    (**(code **)(*(int *)*piVar16 + 0x94))
              ((int *)*piVar16,uVar3,DAT_00016938 + 0x1682e,*puVar12,0,1,iVar13,iVar18,5);
    (**(code **)(*(int *)*piVar16 + 0x94))
              ((int *)*piVar16,uVar3,DAT_0001693c + 0x1684e,*pcVar9,0,1,iVar13,iVar18,6);
    (**(code **)(*(int *)*piVar16 + 0x98))
              ((int *)*piVar16,uVar3,DAT_00016940 + 0x1687c,*puVar17,100,500,iVar18,0,9);
    (**(code **)(*(int *)*piVar16 + 0x98))
              ((int *)*piVar16,uVar3,DAT_00016944 + 0x16894,*puVar21,100,500,iVar18,0,10);
  }
LAB_0001689c:
  if (**(int **)(DAT_00016948 + 0x168a4) != local_28) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_0001694c @ 0001694c

void FUN_0001694c(void)

{
  byte bVar1;
  char cVar2;
  int *piVar3;
  uint uVar4;
  undefined4 *puVar5;
  char *__dest;
  uint extraout_r1;
  int iVar6;
  uint uVar7;
  int iVar8;
  byte *pbVar9;
  char *pcVar10;
  size_t __n;
  size_t sVar11;
  code *pcVar12;
  byte *pbVar13;
  int iVar14;
  undefined4 uVar15;
  undefined4 uVar16;
  undefined4 uVar17;
  undefined4 uVar18;
  undefined4 *puVar19;
  undefined4 local_148;
  undefined4 local_144;
  undefined4 local_140;
  undefined4 local_13c;
  char local_138 [260];
  int local_34;
  
  local_34 = **(int **)(DAT_00016b7c + 0x16966);
  (**(code **)(DAT_00016b78 + 0x16960))();
  if (*(char *)(DAT_00016b80 + 0x16972) == '\x01') {
    pcVar10 = (char *)(DAT_00016b84 + 0x1697c);
    pbVar9 = (byte *)(DAT_00016b88 + 0x16984);
    if (*pcVar10 == '\0') {
      uVar4 = 0;
      uVar7 = extraout_r1;
      if ((*(byte *)(DAT_00016b8c + 0x16a26) & 1) == 0) goto LAB_000169d4;
LAB_00016a2a:
      if (*(char *)(DAT_00016bb8 + 0x16a30) != '\0') goto LAB_00016a9a;
      uVar4 = DAT_00016bbc + 0x16a3c;
      iVar6 = DAT_00016bc4 + 0x16a46;
      uVar7 = DAT_00016bc0 + 0x16a40;
      if (*pbVar9 == 0) {
        uVar7 = uVar4;
      }
      iVar8 = DAT_00016bc8 + 0x16a4c;
    }
    else {
      __aeabi_memclr8(local_138,0x100);
      piVar3 = *(int **)(DAT_00016b98 + 0x1699c);
      pcVar12 = *(code **)(*piVar3 + 0x94);
      iVar6 = DAT_00016b9c + 0x169aa;
      *pcVar10 = '\0';
      iVar6 = (*pcVar12)(piVar3,iVar6,local_138,0x100);
      pbVar13 = (byte *)(DAT_00016ba0 + 0x169ba);
      *pbVar13 = (byte)iVar6;
      if ((iVar6 != 0 && local_138[0] != '\0') &&
         (*(bool *)(DAT_00016ba4 + 0x16ac4) = local_138[0] == '-', iVar6 = DAT_00016ba8,
         local_138[0] != '-')) {
        sVar11 = 0;
        do {
          __n = sVar11;
          sVar11 = __n + 1;
        } while (local_138[__n] != '\n');
        pcVar10 = (char *)(DAT_00016ba8 + 0x16ae8);
        pcVar10[0] = '\0';
        pcVar10[1] = '\0';
        pcVar10[2] = '\0';
        pcVar10[3] = '\0';
        *(undefined4 *)(iVar6 + 0x16aec) =
             *(undefined4 *)((undefined1  [16])0x0 + (undefined1  [16])0x4);
        *(undefined4 *)(iVar6 + 0x16af0) =
             *(undefined4 *)((undefined1  [16])0x0 + (undefined1  [16])0x8);
        *(undefined4 *)(iVar6 + 0x16af4) =
             *(undefined4 *)((undefined1  [16])0x0 + (undefined1  [16])0xc);
        __strncpy_chk2(pcVar10,local_138,__n,0x10,0x100);
        iVar6 = strncmp(pcVar10,(char *)(DAT_00016bac + 0x16b02),__n);
        cVar2 = local_138[sVar11];
        *pbVar9 = iVar6 != 0;
        iVar6 = DAT_00016bb0;
        if (cVar2 != '\0') {
          pcVar10 = local_138 + sVar11;
          iVar8 = 0;
          do {
            iVar14 = iVar8;
            iVar8 = iVar14 + 1;
          } while (local_138[sVar11 + iVar14] != '\n');
          uVar15 = 0;
          uVar16 = *(undefined4 *)((undefined1  [16])0x0 + (undefined1  [16])0x4);
          uVar17 = *(undefined4 *)((undefined1  [16])0x0 + (undefined1  [16])0x8);
          uVar18 = *(undefined4 *)((undefined1  [16])0x0 + (undefined1  [16])0xc);
          puVar5 = (undefined4 *)(DAT_00016bb0 + 0x16b36);
          *puVar5 = 0;
          *(undefined4 *)(iVar6 + 0x16b3a) = uVar16;
          *(undefined4 *)(iVar6 + 0x16b3e) = uVar17;
          *(undefined4 *)(iVar6 + 0x16b42) = uVar18;
          *(undefined4 *)(iVar6 + 0x16b46) = 0;
          *(undefined4 *)(iVar6 + 0x16b4a) = uVar16;
          *(undefined4 *)(iVar6 + 0x16b4e) = uVar17;
          *(undefined4 *)(iVar6 + 0x16b52) = uVar18;
          __strncpy_chk(puVar5,pcVar10,iVar14,0x20);
          iVar6 = DAT_00016bb4;
          __dest = (char *)(DAT_00016bb4 + 0x16b4c);
          *(undefined4 *)__dest = uVar15;
          *(undefined4 *)(iVar6 + 0x16b50) = uVar16;
          *(undefined4 *)(iVar6 + 0x16b54) = uVar17;
          *(undefined4 *)(iVar6 + 0x16b58) = uVar18;
          *(undefined4 *)(iVar6 + 0x16b5c) = uVar15;
          *(undefined4 *)(iVar6 + 0x16b60) = uVar16;
          *(undefined4 *)(iVar6 + 0x16b64) = uVar17;
          *(undefined4 *)(iVar6 + 0x16b68) = uVar18;
          *(undefined4 *)(iVar6 + 0x16b6c) = uVar15;
          *(undefined4 *)(iVar6 + 0x16b70) = uVar16;
          *(undefined4 *)(iVar6 + 0x16b74) = uVar17;
          *(undefined4 *)((int)&DAT_00016b78 + iVar6) = uVar18;
          *(undefined4 *)((int)&DAT_00016b7c + iVar6) = uVar15;
          *(undefined4 *)((int)&DAT_00016b80 + iVar6) = uVar16;
          *(undefined4 *)((int)&DAT_00016b84 + iVar6) = uVar17;
          *(undefined4 *)((int)&DAT_00016b88 + iVar6) = uVar18;
          *(undefined4 *)((int)&DAT_00016b8c + iVar6) = uVar15;
          *(undefined4 *)((int)&DAT_00016b90 + iVar6) = uVar16;
          *(undefined4 *)((int)&DAT_00016b94 + iVar6) = uVar17;
          *(undefined4 *)((int)&DAT_00016b98 + iVar6) = uVar18;
          *(undefined4 *)((int)&DAT_00016b9c + iVar6) = uVar15;
          *(undefined4 *)((int)&DAT_00016ba0 + iVar6) = uVar16;
          *(undefined4 *)((int)&DAT_00016ba4 + iVar6) = uVar17;
          *(undefined4 *)((int)&DAT_00016ba8 + iVar6) = uVar18;
          *(undefined4 *)((int)&DAT_00016bac + iVar6) = uVar15;
          *(undefined4 *)((int)&DAT_00016bb0 + iVar6) = uVar16;
          *(undefined4 *)((int)&DAT_00016bb4 + iVar6) = uVar17;
          *(undefined4 *)((int)&DAT_00016bb8 + iVar6) = uVar18;
          *(undefined4 *)((int)&DAT_00016bbc + iVar6) = uVar15;
          *(undefined4 *)((int)&DAT_00016bc0 + iVar6) = uVar16;
          *(undefined4 *)((int)&DAT_00016bc4 + iVar6) = uVar17;
          *(undefined4 *)((int)&DAT_00016bc8 + iVar6) = uVar18;
          strncpy(__dest,pcVar10 + iVar14 + 1,0x80);
        }
      }
      bVar1 = *pbVar13;
      uVar4 = (uint)bVar1;
      bVar1 = *pbVar9 & bVar1;
      uVar7 = (uint)bVar1;
      *pbVar9 = bVar1;
      if (uVar4 == 1) goto LAB_00016a2a;
LAB_000169d4:
      iVar6 = DAT_00016b90 + 0x169dc;
      iVar8 = DAT_00016b94 + 0x169de;
    }
    FUN_00016db8(uVar4,uVar7,iVar6,iVar8);
    uVar15 = 0x3fc00000;
    if (*pbVar9 != 0) {
      uVar15 = 0x40000000;
    }
    puVar5 = &local_140;
    local_13c = 0xffffffff;
    puVar19 = &local_13c;
    local_140 = 0;
    iVar6 = DAT_00016bcc + 0x16a8c;
  }
  else {
    FUN_00016db8(*(char *)(DAT_00016b80 + 0x16972),extraout_r1,DAT_00016bd0 + 0x169e4);
    local_144 = 0xff3232ff;
    puVar5 = &local_148;
    puVar19 = &local_144;
    iVar6 = DAT_00016bd4 + 0x16a06;
    local_148 = 0;
    uVar15 = 0x40000000;
  }
  FUN_000128a0(iVar6,0x3ca3d70a,0x3c23d70a,uVar15,1,puVar19,puVar5,0,0,0);
LAB_00016a9a:
  if (**(int **)(DAT_00016bd8 + 0x16aa2) != local_34) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_00016bdc @ 00016bdc

/* WARNING: Restarted to delay deadcode elimination for space: stack */

void FUN_00016bdc(void)

{
  char cVar1;
  int iVar2;
  int *piVar3;
  int iVar4;
  int extraout_r1;
  undefined4 *puVar5;
  undefined4 uVar6;
  int *piVar7;
  bool bVar8;
  undefined8 uVar9;
  stat sStack_80;
  int local_14;
  
  local_14 = **(int **)(DAT_00016cd4 + 0x16bee);
  (**(code **)(DAT_00016cd0 + 0x16be8))();
  iVar2 = *(int *)(DAT_00016cd8 + 0x16bfa);
  iVar4 = *(int *)(iVar2 + 0x24);
  if (iVar4 == 0) {
    iVar2 = *(int *)(iVar2 + 0x2c);
  }
  if (iVar4 == 0 && iVar2 == 0) {
    iVar4 = 1;
    *(undefined1 *)(DAT_00016cdc + 0x16ca8) = 1;
  }
  DataMemoryBarrier(0x1b);
  puVar5 = (undefined4 *)(DAT_00016ce4 + 0x16c16);
  if ((*(byte *)(DAT_00016ce0 + 0x16c0c) & 1) == 0) {
    uVar9 = __cxa_guard_acquire(DAT_00016ce8 + 0x16cb0);
    iVar4 = (int)((ulonglong)uVar9 >> 0x20);
    if ((int)uVar9 != 0) {
      uVar6 = (**(code **)(**(int **)(DAT_00016cec + 0x16cbc) + 4))();
      iVar2 = DAT_00016cf0;
      *puVar5 = uVar6;
      __cxa_guard_release(iVar2 + 0x16cca);
      iVar4 = extraout_r1;
    }
  }
  piVar7 = (int *)(DAT_00016cf4 + 0x16c20);
  FUN_00016db8(*(undefined4 *)*piVar7,iVar4,DAT_00016cf8 + 0x16c2a,*puVar5);
  iVar2 = stat((char *)(DAT_00016cfc + 0x16c34),&sStack_80);
  if (iVar2 == 0) {
    if (sStack_80.__unused4 == *(ulong *)(DAT_00016d00 + 0x16c52)) goto LAB_00016c56;
    uVar6 = 0;
    *(ulong *)(DAT_00016d00 + 0x16c52) = sStack_80.__unused4;
  }
  else {
    if ((iVar2 != -1) || (piVar3 = (int *)__errno(), *piVar3 != 2)) {
LAB_00016c56:
      if (**(int **)(DAT_00016d08 + 0x16c5e) == local_14) {
        return;
      }
      goto LAB_00016c9c;
    }
    uVar6 = 1;
  }
  puVar5 = (undefined4 *)*piVar7;
  (**(code **)(*(int *)puVar5[2] + 4))((int *)puVar5[2],puVar5[1],*puVar5);
  FUN_00017abc(uVar6);
  puVar5 = (undefined4 *)*piVar7;
  if (**(int **)(DAT_00016d04 + 0x16c8c) == local_14) {
    cVar1 = *(char *)(puVar5 + 3);
    bVar8 = cVar1 == '\x01';
    if (bVar8) {
      cVar1 = *(char *)((int)puVar5 + 0xd);
    }
    if (bVar8 && cVar1 == '\x01') {
      *(undefined1 *)((int)puVar5 + 0xd) = 0;
                    /* WARNING: Could not recover jumptable at 0x00012b9e. Too many branches */
                    /* WARNING: Treating indirect jump as call */
      (**(code **)(*(int *)puVar5[2] + 8))((int *)puVar5[2],puVar5[1],*puVar5);
      return;
    }
    return;
  }
LAB_00016c9c:
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}


// ==== FUN_00016d0c @ 00016d0c

void FUN_00016d0c(undefined4 param_1,float *param_2)

{
  int iVar1;
  uint in_fpscr;
  float fVar2;
  float fVar3;
  
  (**(code **)(DAT_00016d64 + 0x16d18))(param_1,param_2);
  fVar3 = *param_2;
  fVar2 = (float)VectorSignedToFloat(*(int *)(DAT_00016d68 + 0x16d2a) / 100,
                                     (byte)(in_fpscr >> 0x16) & 3);
  iVar1 = *(int *)(**(int **)(DAT_00016d6c + 0x16d46) + 0x1e8);
  *param_2 = fVar3 * fVar2;
  if (iVar1 != 0) {
    *param_2 = -(fVar3 * fVar2);
  }
  return;
}


// ==== FUN_00016d70 @ 00016d70

void FUN_00016d70(undefined4 param_1,int param_2)

{
  uint in_fpscr;
  float fVar1;
  
  (**(code **)(DAT_00016db0 + 0x16d7c))(param_1,param_2);
  fVar1 = (float)VectorSignedToFloat(*(int *)(DAT_00016db4 + 0x16d92) / 100,
                                     (byte)(in_fpscr >> 0x16) & 3);
  *(float *)(param_2 + 4) = *(float *)(param_2 + 4) * fVar1;
  return;
}


// ==== FUN_00016db8 @ 00016db8

void FUN_00016db8(undefined4 param_1,undefined4 param_2,undefined4 param_3,undefined4 param_4)

{
  int iVar1;
  undefined4 local_4;
  
  iVar1 = **(int **)(DAT_00016e04 + 0x16dd2);
  local_4 = param_4;
  __vsprintf_chk(DAT_00016e00 + 0x16dce,0,0xff,param_3,&local_4,&local_4);
  if (**(int **)(DAT_00016e08 + 0x16dec) != iVar1) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_00016e0c @ 00016e0c

void FUN_00016e0c(void)

{
  code *UNRECOVERED_JUMPTABLE;
  
  UNRECOVERED_JUMPTABLE = (code *)FUN_000126ac(DAT_00016e24 + 0x16e16);
                    /* WARNING: Could not recover jumptable at 0x00016e22. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*UNRECOVERED_JUMPTABLE)(DAT_00016e28 + 0x16e20);
  return;
}


// ==== _INIT_0 @ 00016e2c

void _INIT_0(void)

{
  undefined4 uVar1;
  code *pcVar2;
  
  uVar1 = dlopen(DAT_00016e54 + 0x16e38,0);
  pcVar2 = (code *)dlsym(uVar1,DAT_00016e58 + 0x16e40);
  uVar1 = (*pcVar2)(DAT_00016e5c + 0x16e4a);
  *(undefined4 *)(DAT_00016e60 + 0x16e50) = uVar1;
  return;
}


// ==== _INIT_4 @ 00016e64

int _INIT_4(void)

{
  char cVar1;
  undefined4 uVar2;
  int iVar3;
  int iVar4;
  char *pcVar5;
  undefined4 *puVar6;
  int *piVar7;
  
  pcVar5 = (char *)(DAT_00016ea4 + 0x16e78);
  ModInfo::ModInfo((ModInfo *)(DAT_00016ea0 + 0x16e74),pcVar5,(char *)(DAT_00016ea8 + 0x16e7c),
                   (char *)(DAT_00016eac + 0x16e7e),(char *)(DAT_00016eb0 + 0x16e80));
  FUN_00012b24(DAT_00016eb4 + 0x16e8e,pcVar5);
  iVar3 = DAT_00016eb8 + 0x16e96;
  puVar6 = (undefined4 *)(DAT_0001612c + 0x16084);
  cVar1 = (**(code **)(*(int *)*puVar6 + 0xa8))
                    ((int *)*puVar6,DAT_00016130 + 0x16092,DAT_00016134 + 0x16094);
  pcVar5 = (char *)(DAT_00016138 + 0x1609e);
  piVar7 = (int *)*puVar6;
  iVar4 = DAT_0001613c + 0x160a2;
  *pcVar5 = cVar1;
  uVar2 = FUN_000126ac(iVar4);
  (**(code **)(*piVar7 + 0x18))(piVar7,uVar2,DAT_00016140 + 0x160b6,DAT_00016144 + 0x160b8);
  if (*pcVar5 == '\x01') {
    piVar7 = (int *)(DAT_00016148 + 0x160cc);
    (**(code **)(*(int *)*puVar6 + 0xc0))
              ((int *)*puVar6,*piVar7 + 0x3f6ef9,DAT_0001614c + 0x160e0,DAT_00016150 + 0x160e2);
    (**(code **)(*(int *)*puVar6 + 0xc0))
              ((int *)*puVar6,*piVar7 + 0x3fc4ab,DAT_00016154 + 0x16100,DAT_00016158 + 0x16102);
    (**(code **)(*(int *)*puVar6 + 0xc0))
              ((int *)*puVar6,*piVar7 + 0x3fc79d,DAT_0001615c + 0x16120,DAT_00016160 + 0x16122);
  }
  return iVar3;
}


// ==== FUN_00016ebc @ 00016ebc

void FUN_00016ebc(int param_1)

{
  uint uVar1;
  uint in_fpscr;
  float fVar2;
  float fVar3;
  float fVar4;
  float fVar5;
  float fVar6;
  float fVar7;
  undefined1 local_20;
  undefined1 local_1f;
  undefined1 local_1e;
  byte local_1d;
  int local_1c;
  
  local_1c = **(int **)(DAT_00017068 + 0x16ed4);
  fVar2 = *(float *)(*(int *)(*(int *)(DAT_0001706c + 0x16ee8) +
                             (uint)**(byte **)(DAT_00017064 + 0x16ed2) * 0x194) + 0x54c);
  if ((*(char *)(DAT_00017070 + 0x16f19) == '\x01') && (**(short **)(DAT_00017074 + 0x16f04) == 3))
  {
    if (**(uint **)(DAT_00017078 + 0x16f18) % 0x15e < 0x97) goto LAB_0001703c;
  }
  else if ((*(char *)(DAT_00017070 + 0x16f62) == '\0') &&
          (uVar1 = in_fpscr & 0xfffffff | (uint)(fVar2 == 1.0) << 0x1e,
          in_fpscr = uVar1 | (uint)(1.0 <= fVar2) << 0x1d,
          !(bool)((byte)(in_fpscr >> 0x1d) & 1) || SUB41(uVar1 >> 0x1e,0))) goto LAB_0001703c;
  fVar3 = (float)VectorUnsignedToFloat
                           ((uint)*(byte *)((short)(ushort)**(byte **)(DAT_00017064 + 0x16ed2) *
                                            0x194 + *(int *)(DAT_0001706c + 0x16ee8) + 0x150),
                            (byte)(in_fpscr >> 0x16) & 3);
  fVar5 = (float)VectorSignedToFloat(*(undefined4 *)(*(int *)(DAT_0001707c + 0x16f6e) + 4),
                                     (byte)(in_fpscr >> 0x16) & 3);
  fVar6 = (float)VectorSignedToFloat(*(undefined4 *)(*(int *)(DAT_0001707c + 0x16f6e) + 8),
                                     (byte)(in_fpscr >> 0x16) & 3);
  fVar4 = 1.0;
  if (fVar3 / DAT_00017058 < 1.0) {
    fVar4 = fVar3 / DAT_00017058;
  }
  local_1d = *(byte *)(DAT_00017070 + 0x16f0d);
  if (*(byte *)(param_1 + 0x4c) < *(byte *)(DAT_00017070 + 0x16f0d)) {
    local_1d = *(byte *)(param_1 + 0x4c);
  }
  fVar7 = *(float *)(DAT_00017070 + 0x16f36) * fVar5;
  fVar4 = fVar7 * fVar4;
  if (*(char *)(DAT_00017070 + 0x16ef6) != '\0') {
    fVar4 = fVar7;
  }
  fVar7 = *(float *)(DAT_00017070 + 0x16f22) * fVar6;
  local_1e = *(undefined1 *)(DAT_00017070 + 0x16f0c);
  if (*(char *)(DAT_00017070 + 0x16f63) != '\0') {
    fVar7 = fVar7 + *(float *)(DAT_00017070 + 0x16f66) * DAT_0001705c;
  }
  local_1f = (undefined1)((ushort)*(undefined2 *)(DAT_00017070 + 0x16f0a) >> 8);
  local_20 = (undefined1)*(undefined2 *)(DAT_00017070 + 0x16f0a);
  FUN_000170e4(*(float *)(DAT_00017070 + 0x16f1e) * fVar5 - fVar4,fVar7,fVar4,
               *(float *)(DAT_00017070 + 0x16f3a) * fVar6,*(undefined1 *)(DAT_00017070 + 0x16f06),
               (fVar2 / fVar3) * DAT_00017060,0,*(undefined1 *)(DAT_00017070 + 0x16ef3),
               *(undefined4 *)(DAT_00017070 + 0x16efa),*(undefined4 *)(DAT_00017070 + 0x16f4e),
               &local_20);
LAB_0001703c:
  if (**(int **)(DAT_00017080 + 0x17044) != local_1c) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_00017084 @ 00017084

void FUN_00017084(float *param_1,int param_2)

{
  int iVar1;
  uint in_fpscr;
  float fVar2;
  float fVar3;
  
  iVar1 = *(int *)(FUN_00017084 + DAT_000170e0 + 6);
  fVar2 = (float)VectorSignedToFloat(*(undefined4 *)(iVar1 + 4),(byte)(in_fpscr >> 0x16) & 3);
  if (param_2 == 0) {
    fVar2 = *param_1 * fVar2;
    fVar3 = (float)VectorSignedToFloat(*(undefined4 *)(iVar1 + 8),(byte)(in_fpscr >> 0x16) & 3);
  }
  else {
    fVar3 = (float)VectorSignedToFloat(*(undefined4 *)(iVar1 + 8),(byte)(in_fpscr >> 0x16) & 3);
    fVar3 = fVar3 / DAT_000170dc;
    fVar2 = *param_1 * (fVar2 / DAT_000170d8);
  }
  *param_1 = fVar2;
  param_1[1] = param_1[1] * fVar3;
  return;
}


// ==== FUN_000170e4 @ 000170e4

void FUN_000170e4(float param_1,float param_2,float param_3,float param_4,int param_5,float param_6,
                 undefined4 param_7,int param_8,uint param_9,float param_10,byte *param_11)

{
  float fVar1;
  undefined4 uVar2;
  int iVar3;
  uint in_fpscr;
  float fVar4;
  float fVar5;
  float fVar6;
  float fVar7;
  undefined2 local_9c;
  undefined1 local_9a;
  byte local_99;
  undefined2 local_98;
  byte local_96;
  byte local_95;
  float local_94;
  float local_90;
  float local_8c;
  float local_88;
  undefined1 auStack_82 [20];
  undefined1 auStack_6e [10];
  int local_64;
  
  local_64 = **(int **)(DAT_00017454 + 0x170fe);
  FUN_00012840(1,0);
  FUN_00012840(7,1);
  fVar1 = DAT_00017440;
  fVar7 = param_2 + param_4;
  fVar6 = param_1 + param_3;
  in_fpscr = in_fpscr & 0xfffffff;
  if (param_6 < 0.0) {
    param_6 = DAT_00017440;
  }
  local_94 = param_1;
  if (param_5 == 0) {
    fVar5 = param_1 + (param_6 / DAT_00017444) * param_3;
    if (fVar6 < fVar5) {
      fVar5 = fVar6;
    }
    local_90 = param_2;
    local_8c = fVar5;
    local_88 = fVar7;
    FUN_00012860(&local_94,param_11);
    local_94 = fVar5;
    local_90 = param_2;
    local_88 = fVar7;
  }
  else {
    fVar5 = param_2 + ((DAT_00017444 - param_6) / DAT_00017444) * param_4;
    if (fVar7 < fVar5) {
      fVar5 = fVar7;
    }
    local_90 = fVar7;
    local_8c = fVar6;
    local_88 = fVar5;
    FUN_00012860(&local_94,param_11);
    local_94 = param_1;
    local_90 = fVar5;
    local_88 = param_2;
  }
  local_95 = param_11[3];
  local_98 = CONCAT11(param_11[1] >> 1,*param_11 >> 1);
  local_96 = param_11[2] >> 1;
  local_8c = fVar6;
  FUN_00012860(&local_94,&local_98);
  if (param_9 - 1 < 0x1e) {
    fVar4 = (float)VectorSignedToFloat(*(undefined4 *)(*(int *)(DAT_00017458 + 0x17218) + 8),
                                       (byte)(in_fpscr >> 0x16) & 3);
    fVar4 = fVar4 / DAT_00017448;
    if (fVar4 < 1.0) {
      fVar4 = 1.0;
    }
    param_10 = param_10 * fVar4;
    local_95 = param_11[3];
    iVar3 = param_9 * 0x20000000;
    local_96 = 0;
    local_98 = 0;
    if (iVar3 < 0) {
      local_8c = param_10;
      if (param_9 < 0x10) {
        local_8c = fVar1;
      }
      local_94 = param_10;
      if (-1 < (int)(param_9 << 0x1e)) {
        local_94 = fVar1;
      }
      local_88 = param_2 + fVar4;
      local_90 = param_2 - param_10;
      local_8c = fVar6 + local_8c;
      local_94 = param_1 - local_94;
      FUN_00012860(&local_94,&local_98);
    }
    if ((int)(param_9 << 0x1c) < 0) {
      local_8c = param_10;
      if (param_9 < 0x10) {
        local_8c = fVar1;
      }
      local_94 = param_10;
      if (-1 < (int)(param_9 << 0x1e)) {
        local_94 = fVar1;
      }
      local_88 = fVar7 - fVar4;
      local_90 = fVar7 + param_10;
      local_8c = fVar6 + local_8c;
      local_94 = param_1 - local_94;
      FUN_00012860(&local_94,&local_98);
    }
    if ((int)(param_9 << 0x1e) < 0) {
      local_88 = param_10;
      if (-1 < (int)(param_9 * 0x10000000)) {
        local_88 = fVar1;
      }
      local_90 = param_10;
      if (-1 < iVar3) {
        local_90 = fVar1;
      }
      local_8c = param_1 + fVar4;
      local_94 = param_1 - param_10;
      local_88 = fVar7 + local_88;
      local_90 = param_2 - local_90;
      FUN_00012860(&local_94,&local_98);
    }
    if (0xf < param_9) {
      local_88 = param_10;
      if (-1 < (int)(param_9 * 0x10000000)) {
        local_88 = fVar1;
      }
      local_8c = fVar6 + param_10;
      if (-1 < iVar3) {
        param_10 = fVar1;
      }
      local_94 = fVar6 - fVar4;
      local_88 = fVar7 + local_88;
      local_90 = param_2 - param_10;
      FUN_00012860(&local_94,&local_98);
    }
  }
  if (param_8 != 0) {
    FUN_000183ac(auStack_6e);
    FUN_00012880(auStack_6e,auStack_82);
    FUN_0001b238(param_1);
    FUN_0001b254(fVar6);
    local_99 = param_11[3];
    local_9a = 0;
    local_9c = 0;
    FUN_0001b1d4(&local_9c);
    FUN_0001b2d4(0);
    FUN_0001b21c(1);
    FUN_0001b1b8(param_4 * DAT_0001744c);
    uVar2 = 2;
    param_1 = param_1 + DAT_00017450;
    if (fVar5 < param_1) {
      uVar2 = 1;
    }
    FUN_0001b348(uVar2);
    if (fVar5 < param_1) {
      fVar5 = fVar5 + 5.0;
    }
    FUN_0001b3a8(fVar5,param_2 + 2.0,auStack_82);
  }
  if (**(int **)(DAT_0001745c + 0x17354) != local_64) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_00017478 @ 00017478

void FUN_00017478(int param_1)

{
  undefined2 uVar1;
  byte bVar2;
  undefined1 uVar3;
  byte bVar4;
  int iVar5;
  float fVar6;
  char *pcVar7;
  uint in_fpscr;
  float fVar8;
  float fVar9;
  float fVar10;
  float fVar11;
  float fVar12;
  float fVar13;
  undefined1 local_60;
  undefined1 local_5f;
  undefined1 local_5e;
  byte local_5d;
  int local_5c;
  
  iVar5 = DAT_000175f8;
  pcVar7 = (char *)(DAT_000175f0 + 0x1748e);
  local_5c = **(int **)(DAT_000175f4 + 0x17496);
  if (*pcVar7 == '\x01') {
    if (((*(char *)(DAT_000175f8 + 0x174d1) != '\x01') ||
        (**(short **)(DAT_000175fc + 0x174b0) != 10)) ||
       (0x96 < **(uint **)(DAT_00017600 + 0x174c4) % 0x15e)) {
      bVar2 = *(byte *)(param_1 + 0x4c);
      fVar8 = *(float *)(DAT_000175f8 + 0x174e4);
      fVar9 = *(float *)(DAT_000175f8 + 0x174e8);
      fVar10 = *(float *)(DAT_000175f8 + 0x174fc);
      fVar11 = *(float *)(DAT_000175f8 + 0x17500);
      uVar3 = *(undefined1 *)(DAT_000175f8 + 0x174ca);
      fVar12 = (float)VectorSignedToFloat(*(undefined4 *)(*(int *)(DAT_00017604 + 0x174e6) + 4),
                                          (byte)(in_fpscr >> 0x16) & 3);
      bVar4 = *(byte *)(DAT_000175f8 + 0x174cb);
      fVar13 = (float)VectorSignedToFloat(*(undefined4 *)(*(int *)(DAT_00017604 + 0x174e6) + 8),
                                          (byte)(in_fpscr >> 0x16) & 3);
      uVar1 = *(undefined2 *)(DAT_000175f8 + 0x174c8);
      fVar6 = (float)FUN_00012808(8);
      fVar9 = fVar9 * fVar13;
      fVar10 = fVar10 * fVar12;
      if (*(char *)(iVar5 + 0x17519) != '\0') {
        fVar9 = fVar9 + *(float *)(iVar5 + 0x1751c) * DAT_000175e8;
      }
      local_5f = (undefined1)((ushort)uVar1 >> 8);
      local_60 = (undefined1)uVar1;
      local_5d = bVar4;
      if (bVar2 < bVar4) {
        local_5d = bVar2;
      }
      local_5e = uVar3;
      FUN_000170e4(fVar8 * fVar12 - fVar10,fVar9,fVar10,fVar11 * fVar13,
                   *(undefined1 *)(iVar5 + 0x174be),
                   (*(float *)(*(int *)(*(int *)(*(int *)(DAT_0001760c + 0x17538) +
                                                (uint)**(byte **)(DAT_00017608 + 0x17526) * 0x194) +
                                       0x444) + 0x44) / fVar6) * DAT_000175ec,0,
                   *(undefined1 *)(iVar5 + 0x174ab),*(undefined4 *)(iVar5 + 0x174b8),
                   *(undefined4 *)(iVar5 + 0x1750c),&local_60);
    }
    *pcVar7 = '\0';
  }
  if (**(int **)(DAT_00017610 + 0x175ce) != local_5c) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_0001763c @ 0001763c

void FUN_0001763c(int param_1)

{
  undefined1 uVar1;
  byte bVar2;
  undefined2 uVar3;
  int iVar4;
  int iVar5;
  undefined4 *puVar6;
  float fVar7;
  float *pfVar8;
  int *piVar9;
  int iVar10;
  uint uVar11;
  byte bVar12;
  uint in_fpscr;
  float fVar13;
  float fVar14;
  float fVar15;
  float fVar16;
  float fVar17;
  float fVar18;
  undefined1 local_60;
  undefined1 local_5f;
  undefined1 local_5e;
  byte local_5d;
  int local_5c;
  
  iVar10 = DAT_00017858;
  iVar5 = DAT_00017854;
  iVar4 = DAT_0001784c;
  puVar6 = (undefined4 *)(DAT_00017848 + 0x1765a);
  piVar9 = (int *)(DAT_00017850 + 0x17662);
  *(undefined4 *)(DAT_0001784c + 0x176d4) = 0;
  local_5c = **(int **)(DAT_0001785c + 0x1766e);
  uVar1 = *(undefined1 *)(iVar4 + 0x1767e);
  fVar17 = (float)VectorSignedToFloat(*(undefined4 *)(*(int *)(iVar10 + 0x1766a) + 8),
                                      (byte)(in_fpscr >> 0x16) & 3);
  fVar13 = *(float *)(iVar4 + 0x17694);
  fVar18 = (float)VectorSignedToFloat(*(undefined4 *)(*(int *)(iVar10 + 0x1766a) + 4),
                                      (byte)(in_fpscr >> 0x16) & 3);
  fVar14 = *(float *)(iVar4 + 0x17698);
  fVar15 = *(float *)(iVar4 + 0x176ac);
  iVar10 = *(int *)(*piVar9 + (uint)*(byte *)*puVar6 * 0x194);
  fVar16 = *(float *)(iVar4 + 0x176b0);
  uVar3 = *(undefined2 *)(iVar4 + 0x1767c);
  bVar2 = *(byte *)(param_1 + 0x4c);
  uVar11 = **(uint **)(iVar5 + 0x17666);
  bVar12 = *(byte *)(iVar4 + 0x1767f);
  fVar7 = (float)FUN_000127ec(0x18);
  fVar7 = fVar7 / DAT_00017840;
  fVar15 = fVar15 * fVar18;
  if (1.0 < fVar7) {
    fVar7 = 1.0;
  }
  fVar14 = fVar14 * fVar17;
  fVar7 = fVar15 * fVar7;
  if (*(char *)(iVar4 + 0x17665) != '\0') {
    fVar7 = fVar15;
  }
  fVar16 = fVar16 * fVar17;
  pfVar8 = (float *)(iVar10 + 0x544);
  *(float *)(iVar4 + 0x176e0) = fVar7;
  fVar15 = fVar13 * fVar18 - fVar7;
  *(float *)(iVar4 + 0x176e4) = fVar16;
  if (bVar2 < bVar12) {
    bVar12 = bVar2;
  }
  if ((((*(char *)(iVar4 + 0x176d1) != '\x01') || (*(float *)(param_1 + 0x9c) <= fVar14)) ||
      (fVar14 <= *(float *)(param_1 + 0xa4))) || (1.0 <= fVar15 - *(float *)(param_1 + 0xa0))) {
    fVar13 = fVar14;
  }
  else {
    fVar13 = fVar16 * 0.5 + *(float *)(param_1 + 0x9c);
    if (fVar14 < fVar13) {
      *(float *)(iVar4 + 0x176d4) = (fVar13 - fVar14) * 0.5;
    }
  }
  *(float *)(iVar4 + 0x176d8) = fVar15;
  *(float *)(iVar4 + 0x176dc) = fVar13;
  if (*(char *)(iVar4 + 0x17688) == '\0') {
    fVar14 = *pfVar8;
  }
  else {
    uVar11 = uVar11 % 0x15e;
    if (((**(short **)(DAT_00017860 + 0x1779a) == 4) && (uVar11 < 0x97)) ||
       ((fVar14 = *pfVar8, fVar14 <= 9.0 && (uVar11 < 0x97)))) goto LAB_0001781e;
  }
  local_60 = (undefined1)uVar3;
  local_5f = (undefined1)((ushort)uVar3 >> 8);
  local_5e = uVar1;
  local_5d = bVar12;
  FUN_000170e4(fVar15,fVar13,fVar7,fVar16,*(undefined1 *)(iVar4 + 0x17675),
               (fVar14 / *(float *)(iVar10 + 0x548)) * DAT_00017844,0,
               *(undefined1 *)(iVar4 + 0x17662),*(undefined4 *)(iVar4 + 0x1766c),
               *(undefined4 *)(iVar4 + 0x176c0),&local_60);
LAB_0001781e:
  if (**(int **)(DAT_00017864 + 0x17826) != local_5c) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_00017868 @ 00017868

float FUN_00017868(int param_1,float *param_2,float param_3)

{
  float fVar1;
  
  fVar1 = param_2[1];
  if (((fVar1 < *(float *)(param_1 + 4)) && (*(float *)(param_1 + 0xc) < fVar1)) &&
     (*param_2 - *(float *)(param_1 + 8) < 1.0)) {
    fVar1 = param_3 + *(float *)(param_1 + 4);
    param_2[1] = fVar1;
  }
  return fVar1;
}


// ==== FUN_000178cc @ 000178cc

void FUN_000178cc(undefined4 param_1)

{
  int iVar1;
  
  iVar1 = DAT_0001791c;
  if ((*(int *)(DAT_0001791c + 0x17946) != 0) && (*(char *)(DAT_0001791c + 0x178da) == '\x01')) {
    if (*(char *)(DAT_0001791c + 0x178fe) == '\x01') {
      FUN_00016ebc(*(int *)(DAT_0001791c + 0x17946));
    }
    if (*(char *)(iVar1 + 0x17900) == '\x01') {
      FUN_00017478(*(undefined4 *)(iVar1 + 0x17946));
    }
    if (*(char *)(iVar1 + 0x178ff) == '\x01') {
      FUN_0001763c(*(undefined4 *)(iVar1 + 0x17946));
    }
  }
                    /* WARNING: Could not recover jumptable at 0x0001791a. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (**(code **)(DAT_00017920 + 0x17916))(param_1);
  return;
}


// ==== FUN_00017924 @ 00017924

void FUN_00017924(int param_1)

{
  char *pcVar1;
  byte bVar2;
  undefined4 *puVar3;
  code *pcVar4;
  int iVar5;
  int iVar6;
  byte *pbVar7;
  int *piVar8;
  int *piVar9;
  float *pfVar10;
  undefined4 uVar11;
  int iVar12;
  int *piVar13;
  int iVar14;
  int iVar15;
  char cVar16;
  uint in_fpscr;
  float fVar17;
  undefined4 uVar18;
  float fVar19;
  float fVar20;
  float fVar21;
  float fVar22;
  float fVar23;
  undefined4 uVar24;
  float fVar25;
  undefined8 uVar26;
  undefined1 local_44 [4];
  
  iVar12 = DAT_00017a98;
  puVar3 = (undefined4 *)(DAT_00017a98 + 0x1793a);
  pcVar1 = (char *)(DAT_00017a98 + 0x1793e);
  *(int *)(DAT_00017a98 + 0x179aa) = param_1;
  if (*pcVar1 != '\x01') {
    return;
  }
  if (*(char *)(iVar12 + 0x179a6) == '\x01') {
    *(undefined1 *)(param_1 + 0xb4) = 0;
  }
  fVar17 = (float)VectorSignedToFloat(*(undefined4 *)(*(int *)(DAT_00017a9c + 0x17956) + 4),
                                      (byte)(in_fpscr >> 0x16) & 3);
  fVar19 = (float)VectorSignedToFloat(*(undefined4 *)(*(int *)(DAT_00017a9c + 0x17956) + 8),
                                      (byte)(in_fpscr >> 0x16) & 3);
  fVar20 = *(float *)(DAT_00017aa0 + 0x1796e) * fVar17;
  fVar17 = *(float *)(DAT_00017aa4 + 0x17970) * fVar17;
  fVar21 = *(float *)(DAT_00017aa0 + 0x17972) * fVar19;
  fVar19 = *(float *)(DAT_00017aa4 + 0x17974) * fVar19;
  pbVar7 = *(byte **)(DAT_00017aa8 + 0x17998);
  fVar25 = fVar17 + fVar20;
  fVar17 = fVar17 - fVar20;
  fVar20 = fVar19 - fVar21;
  fVar19 = fVar19 + fVar21;
  *(float *)(param_1 + 0xa0) = fVar25;
  *(float *)(param_1 + 0x98) = fVar17;
  *(float *)(param_1 + 0xa4) = fVar20;
  *(float *)(param_1 + 0x9c) = fVar19;
  piVar13 = (int *)(DAT_00017aac + 0x179c2);
  uVar11 = *(undefined4 *)(*piVar13 + (uint)*pbVar7 * 0x194);
  cVar16 = *(char *)(iVar12 + 0x179b0);
  *puVar3 = uVar11;
  if (cVar16 == '\x01') {
    bVar2 = *(byte *)(param_1 + 0x4c);
    pcVar4 = (code *)FUN_000126ac(DAT_00017ab0 + 0x179d8);
    uVar18 = VectorUnsignedToFloat((uint)bVar2,(byte)(in_fpscr >> 0x16) & 3);
    (*pcVar4)(param_1,uVar11,fVar17,fVar19,fVar25,fVar20,uVar18);
    iVar12 = *(int *)(*piVar13 + 0x194);
    if (iVar12 != 0) {
      fVar19 = *(float *)(param_1 + 0x9c);
      fVar20 = *(float *)(param_1 + 0xa4);
      uVar18 = *(undefined4 *)(param_1 + 0x98);
      uVar11 = *(undefined4 *)(param_1 + 0xa0);
      fVar17 = ABS(*(float *)(param_1 + 0x2c) - *(float *)(param_1 + 0x24));
      uVar24 = VectorUnsignedToFloat((uint)*(byte *)(param_1 + 0x4c),(byte)(in_fpscr >> 0x16) & 3);
      pcVar4 = (code *)FUN_000126ac(DAT_00017ab4 + 0x17a24);
      (*pcVar4)(param_1,iVar12,uVar18,fVar17 + fVar19,uVar11,fVar20 + fVar17,uVar24);
    }
  }
  iVar12 = *(int *)(param_1 + 0x88);
  if (iVar12 != 0) {
    uVar26 = *(undefined8 *)(param_1 + 0xa0);
    *(undefined8 *)(iVar12 + 0xc) = *(undefined8 *)(param_1 + 0x98);
    *(undefined8 *)(iVar12 + 0x14) = uVar26;
  }
  (**(code **)(DAT_00017ab8 + 0x17a76))(param_1);
  iVar12 = **(int **)(DAT_00019738 + 0x1943c);
  if (*(char *)(DAT_00019734 + 0x19434) == '\x01') {
    iVar14 = *(int *)((short)(ushort)**(byte **)(DAT_0001973c + 0x19450) * 0x194 +
                      *(int *)(DAT_00019740 + 0x19452) + 0xb8);
    DataMemoryBarrier(0x1b);
    piVar13 = (int *)(DAT_00019748 + 0x1946e);
    if (((*(byte *)(DAT_00019744 + 0x19460) & 1) == 0) &&
       (iVar6 = __cxa_guard_acquire(DAT_0001974c + 0x196d6), iVar15 = DAT_00019750, iVar6 != 0)) {
      *piVar13 = iVar14;
      __cxa_guard_release(iVar15 + 0x196e6);
    }
    iVar15 = **(int **)(DAT_00019754 + 0x1947a);
    DataMemoryBarrier(0x1b);
    piVar8 = (int *)(DAT_0001975c + 0x1948c);
    if (((*(byte *)(DAT_00019758 + 0x1947c) & 1) == 0) &&
       (iVar5 = __cxa_guard_acquire(DAT_00019760 + 0x196f2), iVar6 = DAT_00019764, iVar5 != 0)) {
      *piVar8 = iVar15;
      __cxa_guard_release(iVar6 + 0x19706);
    }
    DataMemoryBarrier(0x1b);
    pfVar10 = (float *)(DAT_0001976c + 0x194a0);
    if (((*(byte *)(DAT_00019768 + 0x19496) & 1) == 0) &&
       (iVar5 = __cxa_guard_acquire(DAT_00019770 + 0x19714), iVar6 = DAT_00019778, iVar5 != 0)) {
      *pfVar10 = *(float *)(DAT_00019774 + 0x19724);
      __cxa_guard_release(iVar6 + 0x1972c);
    }
    iVar6 = DAT_00019780;
    piVar9 = (int *)(DAT_0001977c + 0x194ae);
    if (iVar14 == *piVar13) {
      iVar5 = *piVar9;
    }
    else {
      iVar5 = iVar14 - *piVar13;
      *piVar13 = iVar14;
      *piVar9 = iVar5;
      *piVar8 = iVar15;
      *pfVar10 = *(float *)(iVar6 + 0x194be);
    }
    if (iVar5 != 0) {
      fVar17 = *(float *)(DAT_00019784 + 0x194d6);
      fVar19 = *(float *)(DAT_00019784 + 0x194da);
      FUN_00017084(local_44 + 0x14,1);
      fVar21 = *(float *)(DAT_0001978c + 0x194f6) * **(float **)(DAT_00019788 + 0x194f0);
      fVar20 = *pfVar10;
      if (fVar20 <= 0.0) {
        fVar25 = fVar21 + fVar20 * (1.0 - fVar21);
      }
      else {
        fVar25 = 1.0;
        if (fVar21 != 1.0) {
          fVar22 = fVar20 + fVar21 * (1.0 - fVar20);
          bVar2 = (byte)(((uint)(fVar22 == 1.0) << 0x1e) >> 0x18);
          cVar16 = -((char)((byte)(((uint)(fVar22 < 1.0) << 0x1f) >> 0x18) | bVar2) >> 7);
          fVar25 = 1.0;
          if (cVar16 != '\0') {
            fVar25 = fVar22;
          }
          fVar23 = 1.0;
          if (!(bool)(bVar2 >> 6) && (bool)cVar16 == NAN(fVar22)) {
            fVar23 = fVar22;
          }
          if (1.0 <= fVar20 != 1.0 < fVar21) {
            fVar25 = fVar23;
          }
        }
      }
      *pfVar10 = fVar25;
      FUN_0001b32c(1);
      FUN_0001b348(*(undefined1 *)(DAT_00019790 + 0x19588));
      FUN_0001b30c(0,0);
      FUN_0001b254(0);
      FUN_00018400(0,fVar17 * *pfVar10);
      FUN_00018410(0,fVar19 * *pfVar10);
      FUN_0001b21c((int)*(short *)(DAT_00019794 + 0x195cc));
      FUN_0001b2f0(*(undefined1 *)(DAT_00019798 + 0x195d8));
      FUN_0001b2b8((int)*(short *)(DAT_0001979c + 0x195e2));
      FUN_0001b270(local_44 + 0x10);
      FUN_0001b2d4((int)*(short *)(DAT_000197a4 + 0x195fc));
      iVar14 = DAT_000197b4 + 0x19640;
      if (*piVar9 < 1) {
        iVar14 = DAT_000197b0 + 0x1963e;
      }
      FUN_000197d0(DAT_000197ac + 0x19614,DAT_000197b0 + 0x1963e,iVar14);
      iVar14 = DAT_000197bc + 0x19650;
      FUN_00012880(DAT_000197b8 + 0x1964e,iVar14);
      FUN_0001b1d4(local_44 + 0xc);
      uVar11 = *(undefined4 *)(DAT_000197c0 + 0x19670);
      fVar17 = *(float *)(DAT_000197c0 + 0x19674);
      FUN_00017084(local_44 + 4,0);
      FUN_0001b3a8(uVar11,fVar17 + *(float *)(DAT_000197c4 + 0x1968a),iVar14);
      FUN_0001b364();
      if (*(uint *)(DAT_000197c8 + 0x196a8) < (uint)(iVar15 - *piVar8)) {
        *piVar9 = 0;
      }
    }
  }
  if (**(int **)(DAT_000197cc + 0x196be) != iVar12) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_00017abc @ 00017abc

void FUN_00017abc(int param_1)

{
  float fVar1;
  undefined1 uVar2;
  void *pvVar3;
  int iVar4;
  float fVar5;
  undefined4 *puVar6;
  int *piVar7;
  int iVar8;
  int iVar9;
  int iVar10;
  int iVar11;
  int iVar12;
  int iVar13;
  int iVar14;
  undefined4 uVar15;
  undefined4 uVar16;
  int iVar17;
  int iVar18;
  int *piVar19;
  int iVar20;
  undefined4 *puVar21;
  int iVar22;
  int iVar23;
  
  puVar21 = (undefined4 *)(DAT_00017d0c + 0x17ad4);
  if (param_1 != 0) {
    iVar18 = DAT_00017d14 + 0x17ae2;
    iVar17 = DAT_00017d18 + 0x17ae6;
    pvVar3 = (void *)FUN_00012ba0(*puVar21,DAT_00017d10 + 0x17ae0,iVar18,iVar17);
    FUN_00012cb8(pvVar3,DAT_00017d1c + 0x17af4);
    operator_delete(pvVar3);
    pvVar3 = (void *)FUN_00012ba0(*puVar21,DAT_00017d20 + 0x17b08,iVar18,iVar17);
    FUN_00012cb8(pvVar3,DAT_00017d24 + 0x17b14);
    operator_delete(pvVar3);
    pvVar3 = (void *)FUN_00012ba0(*puVar21,DAT_00017d28 + 0x17b28,iVar18,iVar17);
    FUN_00012cb8(pvVar3,DAT_00017d2c + 0x17b34);
    operator_delete(pvVar3);
    pvVar3 = (void *)FUN_00012ba0(*puVar21,DAT_00017d30 + 0x17b4a,iVar18,iVar17);
    FUN_00012cb8(pvVar3,DAT_00017d34 + 0x17b54);
    operator_delete(pvVar3);
    FUN_00012b74(*puVar21);
    FUN_00012ba0(*puVar21,DAT_00017d38 + 0x17b6e,DAT_00017d3c + 0x17b70,iVar17);
  }
  uVar2 = FUN_00013070(*puVar21,DAT_00017d40 + 0x17b7e,0,DAT_00017d44 + 0x17b80);
  iVar17 = DAT_00017d48;
  uVar15 = *puVar21;
  iVar8 = DAT_00017d4c + 0x17b92;
  iVar22 = DAT_00017d50 + 0x17b94;
  *(undefined1 *)(DAT_00017d48 + 0x17bfa) = uVar2;
  uVar2 = FUN_00013070(uVar15,iVar8,0,iVar22);
  iVar18 = DAT_00017d54;
  uVar15 = *puVar21;
  *(undefined1 *)(iVar17 + 0x17c02) = uVar2;
  uVar15 = FUN_00012f28(uVar15,iVar18 + 0x17bb6,0x3e4ccccd,iVar22);
  iVar18 = DAT_00017d58;
  uVar16 = *puVar21;
  *(undefined4 *)(iVar17 + 0x17bee) = uVar15;
  uVar15 = FUN_00012e04(uVar16,iVar18 + 0x17bc8,0x1e,iVar22);
  iVar18 = DAT_00017d5c;
  uVar16 = *puVar21;
  *(undefined4 *)(iVar17 + 0x17b9a) = uVar15;
  uVar15 = FUN_00013168(uVar16,iVar18 + 0x17be0,0xffe1e1e1,iVar22);
  uVar16 = *puVar21;
  iVar18 = DAT_00017d60 + 0x17bf0;
  *(undefined4 *)(iVar17 + 0x17baa) = uVar15;
  uVar2 = FUN_00013070(uVar16,iVar18,0,iVar22);
  uVar15 = *puVar21;
  iVar18 = DAT_00017d64 + 0x17c02;
  *(undefined1 *)(iVar17 + 0x17b96) = uVar2;
  iVar4 = FUN_00012e04(uVar15,iVar18,0,iVar22);
  iVar18 = DAT_00017d68;
  uVar15 = *puVar21;
  *(bool *)(iVar17 + 0x17bb6) = iVar4 != 0;
  uVar2 = FUN_00013070(uVar15,iVar18 + 0x17c1e,1,iVar22);
  uVar15 = *puVar21;
  iVar18 = DAT_00017d6c + 0x17c2e;
  *(undefined1 *)(iVar17 + 0x17bb9) = uVar2;
  uVar2 = FUN_00013070(uVar15,iVar18,0,iVar22);
  iVar18 = DAT_00017d70;
  uVar15 = *puVar21;
  *(undefined1 *)(iVar17 + 0x17b93) = uVar2;
  fVar5 = (float)FUN_00012f28(uVar15,iVar18 + 0x17c48,0x42ac0000,iVar22);
  fVar1 = DAT_00017d74;
  uVar15 = *puVar21;
  iVar18 = DAT_00017d78 + 0x17c6c;
  *(float *)(iVar17 + 0x17bbe) = fVar5 / DAT_00017d74;
  fVar5 = (float)FUN_00012f28(uVar15,iVar18,0x41a1999a,iVar22);
  uVar15 = *puVar21;
  iVar18 = DAT_00017d7c + 0x17c88;
  *(float *)(iVar17 + 0x17bc2) = fVar5 / fVar1;
  fVar5 = (float)FUN_00012f28(uVar15,iVar18,0x41480000,iVar22);
  uVar15 = *puVar21;
  iVar18 = DAT_00017d80 + 0x17ca6;
  *(float *)(iVar17 + 0x17bd6) = fVar5 / fVar1;
  fVar5 = (float)FUN_00012f28(uVar15,iVar18,0x40200000,iVar22);
  uVar15 = *puVar21;
  iVar18 = DAT_00017d84 + 0x17cc0;
  *(float *)(iVar17 + 0x17bda) = fVar5 / fVar1;
  uVar2 = FUN_00013070(uVar15,iVar18,0,iVar22);
  uVar15 = *puVar21;
  iVar18 = DAT_00017d88 + 0x17cd4;
  *(undefined1 *)(iVar17 + 0x17ba6) = uVar2;
  iVar18 = FUN_00013070(uVar15,iVar8,0,iVar18);
  puVar6 = (undefined4 *)(DAT_00017d8c + 0x17cea);
  piVar19 = (int *)(DAT_00017d90 + 0x17cee);
  piVar7 = (int *)*puVar6;
  if (iVar18 == 0) {
    (**(code **)(*piVar7 + 0x24))(piVar7,*piVar19 + 0x2bd396,DAT_00018130 + 0x17d9e,4);
  }
  else {
    (**(code **)(*piVar7 + 0x2c))(piVar7,*piVar19 + 0x2bd396,1);
  }
  iVar8 = DAT_00018134 + 0x17db2;
  iVar23 = DAT_00018138 + 0x17db4;
  uVar15 = FUN_00012f28(*puVar21,iVar8,0x3e4ccccd,iVar23);
  iVar18 = DAT_0001813c;
  uVar16 = *puVar21;
  *(undefined4 *)(iVar17 + 0x17bf6) = uVar15;
  uVar15 = FUN_00012e04(uVar16,iVar18 + 0x17dcc,0x1e,iVar23);
  *(undefined4 *)(iVar17 + 0x17ba2) = uVar15;
  iVar20 = DAT_00018140 + 0x17dea;
  uVar15 = FUN_00013168(*puVar21,iVar20,0xffc0a289,iVar23);
  *(undefined4 *)(iVar17 + 0x17bb2) = uVar15;
  iVar22 = DAT_00018144 + 0x17dfe;
  iVar4 = FUN_00012e04(*puVar21,iVar22,0,iVar23);
  *(bool *)(iVar17 + 0x17bb8) = iVar4 != 0;
  iVar4 = DAT_00018148 + 0x17e1a;
  uVar2 = FUN_00013070(*puVar21,iVar4,1,iVar23);
  *(undefined1 *)(iVar17 + 0x17bbb) = uVar2;
  iVar9 = DAT_0001814c + 0x17e30;
  uVar2 = FUN_00013070(*puVar21,iVar9,0,iVar23);
  *(undefined1 *)(iVar17 + 0x17b95) = uVar2;
  iVar10 = DAT_00018150 + 0x17e48;
  fVar5 = (float)FUN_00012f28(*puVar21,iVar10,0x42ac0000,iVar23);
  uVar15 = *puVar21;
  iVar11 = DAT_00018154 + 0x17e66;
  *(float *)(iVar17 + 0x17bce) = fVar5 / fVar1;
  fVar5 = (float)FUN_00012f28(uVar15,iVar11,0x41bb3333,iVar23);
  uVar15 = *puVar21;
  iVar12 = DAT_00018158 + 0x17e88;
  *(float *)(iVar17 + 0x17bd2) = fVar5 / fVar1;
  fVar5 = (float)FUN_00012f28(uVar15,iVar12,0x41480000,iVar23);
  uVar15 = *puVar21;
  iVar13 = DAT_0001815c + 0x17eaa;
  *(float *)(iVar17 + 0x17be6) = fVar5 / fVar1;
  fVar5 = (float)FUN_00012f28(uVar15,iVar13,0x40200000,iVar23);
  uVar15 = *puVar21;
  iVar14 = DAT_00018160 + 0x17ec8;
  *(float *)(iVar17 + 0x17bea) = fVar5 / fVar1;
  uVar2 = FUN_00013070(uVar15,iVar14,0,iVar23);
  *(undefined1 *)(iVar17 + 0x17ba8) = uVar2;
  iVar23 = DAT_00018164 + 0x17ee8;
  uVar15 = FUN_00012f28(*puVar21,iVar8,0x3e4ccccd,iVar23);
  *(undefined4 *)(iVar17 + 0x17bf2) = uVar15;
  uVar15 = FUN_00012e04(*puVar21,iVar18 + 0x17dcc,0x1e,iVar23);
  *(undefined4 *)(iVar17 + 0x17b9e) = uVar15;
  uVar15 = FUN_00013168(*puVar21,iVar20,0xff1d19b4,iVar23);
  *(undefined4 *)(iVar17 + 0x17bae) = uVar15;
  uVar2 = FUN_00013070(*puVar21,DAT_00018168 + 0x17f22,0,iVar23);
  *(undefined1 *)(iVar17 + 0x17b97) = uVar2;
  iVar18 = FUN_00012e04(*puVar21,iVar22,0,iVar23);
  *(bool *)(iVar17 + 0x17bb7) = iVar18 != 0;
  uVar2 = FUN_00013070(*puVar21,iVar4,1,iVar23);
  *(undefined1 *)(iVar17 + 0x17bba) = uVar2;
  uVar2 = FUN_00013070(*puVar21,DAT_0001816c + 0x17f5e,0,iVar23);
  *(undefined1 *)(iVar17 + 0x17c03) = uVar2;
  uVar2 = FUN_00013070(*puVar21,iVar9,0,iVar23);
  *(undefined1 *)(iVar17 + 0x17b94) = uVar2;
  fVar5 = (float)FUN_00012f28(*puVar21,iVar10,0x42ac0000,iVar23);
  uVar15 = *puVar21;
  *(float *)(iVar17 + 0x17bc6) = fVar5 / fVar1;
  fVar5 = (float)FUN_00012f28(uVar15,iVar11,0x41880000,iVar23);
  uVar15 = *puVar21;
  *(float *)(iVar17 + 0x17bca) = fVar5 / fVar1;
  fVar5 = (float)FUN_00012f28(uVar15,iVar12,0x41480000,iVar23);
  uVar15 = *puVar21;
  *(float *)(iVar17 + 0x17bde) = fVar5 / fVar1;
  fVar5 = (float)FUN_00012f28(uVar15,iVar13,0x40200000,iVar23);
  uVar15 = *puVar21;
  *(float *)(iVar17 + 0x17be2) = fVar5 / fVar1;
  uVar2 = FUN_00013070(uVar15,iVar14,0,iVar23);
  *(undefined1 *)(iVar17 + 0x17ba7) = uVar2;
  FUN_00018770();
  FUN_00018bd8();
  FUN_00018ee0();
  FUN_000191f0();
  FUN_00019824();
  FUN_00019f2c();
  FUN_0001a344();
  FUN_0001a7f0();
  FUN_0001aab0();
  FUN_0001aca4();
  iVar18 = FUN_00013070(*puVar21,DAT_00018170 + 0x18032,0,DAT_00018174 + 0x18034);
  piVar7 = (int *)*puVar6;
  iVar4 = *piVar19;
  *(char *)(iVar17 + 0x17c04) = (char)iVar18;
  if (iVar18 == 0) {
    (**(code **)(*piVar7 + 0x24))(piVar7,iVar4 + 0x2bd3f6,DAT_00018178 + 0x1807e,4);
    (**(code **)(*(int *)*puVar6 + 0x24))
              ((int *)*puVar6,*piVar19 + 0x2bd448,DAT_0001817c + 0x18090,4);
  }
  else {
    (**(code **)(*piVar7 + 0xb0))(piVar7,iVar4 + 0x2bd3f6,1);
    (**(code **)(*(int *)*puVar6 + 0xb0))((int *)*puVar6,*piVar19 + 0x2bd448,1);
  }
  iVar4 = DAT_00018184 + 0x180aa;
  fVar5 = (float)FUN_00012f28(*puVar21,DAT_00018180 + 0x180a8,0x42b80000,iVar4);
  iVar17 = DAT_00018188;
  uVar15 = *puVar21;
  iVar18 = DAT_0001818c + 0x180ca;
  *(float *)(DAT_00018188 + 0x180c4) = fVar5 / fVar1;
  fVar5 = (float)FUN_00012f28(uVar15,iVar18,0x41400000,iVar4);
  uVar15 = *puVar21;
  iVar18 = DAT_00018190 + 0x180ea;
  *(float *)(iVar17 + 0x180c8) = fVar5 / fVar1;
  fVar5 = (float)FUN_00012f28(uVar15,iVar18,0x40900000,iVar4);
  iVar17 = DAT_00018194;
  uVar15 = *puVar21;
  iVar18 = DAT_00018198 + 0x1810e;
  *(float *)(DAT_00018194 + 0x18108) = fVar5 / fVar1;
  fVar5 = (float)FUN_00012f28(uVar15,iVar18,0x41200000,iVar4);
  *(float *)(iVar17 + 0x1810c) = fVar5 / fVar1;
  return;
}


// ==== FUN_0001819c @ 0001819c

void FUN_0001819c(void)

{
  undefined4 uVar1;
  int *piVar2;
  undefined4 *puVar3;
  
  FUN_00017abc(1);
  puVar3 = (undefined4 *)(DAT_00018270 + 0x181b2);
  piVar2 = (int *)*puVar3;
  uVar1 = FUN_000126ac(DAT_00018274 + 0x181b4);
  (**(code **)(*piVar2 + 0x18))(piVar2,uVar1,DAT_00018278 + 0x181c6,DAT_0001827c + 0x181c8);
  piVar2 = (int *)*puVar3;
  uVar1 = FUN_000126ac(DAT_00018280 + 0x181d2);
  (**(code **)(*piVar2 + 0x18))(piVar2,uVar1,DAT_00018284 + 0x181e2,DAT_00018288 + 0x181e4);
  piVar2 = (int *)*puVar3;
  uVar1 = FUN_000126ac(DAT_0001828c + 0x181ee);
  (**(code **)(*piVar2 + 0x18))(piVar2,uVar1,DAT_00018290 + 0x181fe,DAT_00018294 + 0x18200);
  piVar2 = (int *)*puVar3;
  uVar1 = FUN_000126ac(DAT_00018298 + 0x1820a);
  (**(code **)(*piVar2 + 0x18))(piVar2,uVar1,DAT_0001829c + 0x1821a,DAT_000182a0 + 0x1821c);
  (**(code **)(*(int *)*puVar3 + 0xc0))
            ((int *)*puVar3,*(int *)(DAT_000182a4 + 0x18230) + 0x437fb7,DAT_000182a8 + 0x18238,
             DAT_000182ac + 0x1823e);
  FUN_00018a78();
  FUN_00018d8c();
  FUN_00019164();
  FUN_000191f2();
  FUN_00019ce8();
  FUN_0001a06c();
  FUN_0001a574();
  FUN_0001a9a4();
  FUN_0001ac64();
  uVar1 = FUN_000126ac(DAT_0001aebc + 0x1aea2);
                    /* WARNING: Could not recover jumptable at 0x0001aeb8. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (**(code **)(**(int **)(DAT_0001aec0 + 0x1aeac) + 0x48))
            (*(int **)(DAT_0001aec0 + 0x1aeac),uVar1,DAT_0001aec4 + 0x1aeb4);
  return;
}


// ==== FUN_000182b0 @ 000182b0

void FUN_000182b0(int param_1,float *param_2,float *param_3)

{
  uint in_fpscr;
  float fVar1;
  float fVar2;
  float fVar3;
  float fVar4;
  float fVar5;
  float fVar6;
  float fVar7;
  float fVar8;
  
  if (param_1 == 0) {
    return;
  }
  fVar1 = (float)VectorSignedToFloat(*(undefined4 *)(*(int *)(DAT_000183a8 + 0x182c4) + 4),
                                     (byte)(in_fpscr >> 0x16) & 3);
  fVar3 = (float)VectorSignedToFloat(*(undefined4 *)(*(int *)(DAT_000183a8 + 0x182c4) + 8),
                                     (byte)(in_fpscr >> 0x16) & 3);
  fVar5 = DAT_00018398;
  if (DAT_00018394 < fVar1 / fVar3) {
    fVar5 = 30.0;
  }
  fVar4 = fVar3 / DAT_0001839c;
  fVar2 = (fVar1 - fVar5) / DAT_000183a0;
  fVar8 = param_3[1];
  fVar7 = *param_3;
  fVar5 = fVar2;
  if (DAT_000183a4 < fVar1 / fVar3) {
    fVar5 = (fVar4 + fVar2) * 0.5;
  }
  fVar6 = param_2[1];
  param_3[1] = fVar8 / fVar4;
  fVar1 = param_2[1];
  fVar3 = *param_2;
  *param_3 = fVar7 / fVar5;
  *param_2 = (fVar7 / fVar5 + *param_2) / fVar2;
  param_2[1] = (fVar8 / fVar4 + fVar1) / fVar4;
  *(float *)(param_1 + 0x20) = fVar3 - fVar7;
  *(float *)(param_1 + 0x2c) = fVar6 - fVar8;
  *(float *)(param_1 + 0x28) = fVar3 + fVar7;
  *(float *)(param_1 + 0x24) = fVar6 + fVar8;
  *(float *)(param_1 + 0x14) = *param_3;
  *(float *)(param_1 + 0x18) = param_3[1];
  *(float *)(param_1 + 0xc) = *param_2;
  *(float *)(param_1 + 0x10) = param_2[1];
  return;
}


// ==== FUN_000183ac @ 000183ac

void FUN_000183ac(undefined4 param_1,undefined4 param_2,undefined4 param_3,undefined4 param_4)

{
  int iVar1;
  undefined4 local_4;
  
  iVar1 = **(int **)(DAT_000183f8 + 0x183c2);
  local_4 = param_4;
  __vsprintf_chk(param_1,0,10,DAT_000183f4 + 0x183c0,&local_4,&local_4);
  if (**(int **)(DAT_000183fc + 0x183de) != iVar1) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_00018400 @ 00018400

void FUN_00018400(undefined4 param_1,undefined4 param_2)

{
  *(undefined4 *)(*(int *)(FUN_00018400 + DAT_0001840c + 6) + 4) = param_2;
  return;
}


// ==== FUN_00018410 @ 00018410

void FUN_00018410(undefined4 param_1,undefined4 param_2)

{
  *(undefined4 *)(*(int *)(FUN_00018410 + DAT_0001841c + 6) + 8) = param_2;
  return;
}


// ==== _INIT_5 @ 00018420

void _INIT_5(void)

{
  undefined4 uVar1;
  
  uVar1 = FUN_000126ac(DAT_00018434 + 0x1842a);
  *(undefined4 *)(DAT_00018438 + 0x18432) = uVar1;
  return;
}


// ==== FUN_0001843c @ 0001843c

void FUN_0001843c(undefined4 param_1,undefined4 param_2,int param_3)

{
  int iVar1;
  int iVar2;
  int *piVar3;
  
  (**(code **)(DAT_000184a8 + 0x18448))(param_1,param_2,param_3);
  DataMemoryBarrier(0x1b);
  piVar3 = (int *)(DAT_000184b0 + 0x1845c);
  if (((*(byte *)(DAT_000184ac + 0x18452) & 1) == 0) &&
     (iVar2 = __cxa_guard_acquire(DAT_000184b4 + 0x18494), iVar1 = DAT_000184b8, iVar2 != 0)) {
    *piVar3 = param_3;
    __cxa_guard_release(iVar1 + 0x184a2);
  }
  iVar1 = DAT_000184c4;
  if (*piVar3 == param_3) {
    iVar2 = (int)*(char *)(*(int *)(DAT_000184bc + 0x18464) + 0x71c);
    if (*(int *)(DAT_000184c0 + 0x1846e) == iVar2) {
      return;
    }
  }
  else {
    iVar2 = (int)*(char *)(*(int *)(DAT_000184bc + 0x18464) + 0x71c);
  }
  *(int *)(DAT_000184c0 + 0x1846e) = iVar2;
  *piVar3 = param_3;
  *(undefined1 *)(iVar1 + 0x18484) = 1;
  return;
}


// ==== FUN_000184c8 @ 000184c8

void FUN_000184c8(undefined4 param_1,undefined4 param_2,int param_3,int param_4)

{
  int iVar1;
  int iVar2;
  int extraout_r2;
  int iVar3;
  int extraout_r2_00;
  int extraout_r2_01;
  int extraout_r2_02;
  int extraout_r2_03;
  int *piVar4;
  int *piVar5;
  
  (**(code **)(DAT_00018578 + 0x184da))(param_1,param_2,param_3);
  DataMemoryBarrier(0x1b);
  piVar4 = (int *)(DAT_00018580 + 0x184ee);
  iVar3 = extraout_r2;
  if (((*(byte *)(DAT_0001857c + 0x184e4) & 1) == 0) &&
     (iVar2 = __cxa_guard_acquire(DAT_00018584 + 0x18544), iVar1 = DAT_00018588,
     iVar3 = extraout_r2_00, iVar2 != 0)) {
    *piVar4 = param_3;
    __cxa_guard_release(iVar1 + 0x18554);
    iVar3 = extraout_r2_01;
  }
  DataMemoryBarrier(0x1b);
  piVar5 = (int *)(DAT_00018590 + 0x18500);
  if (((*(byte *)(DAT_0001858c + 0x184f6) & 1) == 0) &&
     (iVar2 = __cxa_guard_acquire(DAT_00018594 + 0x18562), iVar1 = DAT_00018598,
     iVar3 = extraout_r2_02, iVar2 != 0)) {
    *piVar5 = param_4;
    __cxa_guard_release(iVar1 + 0x18572);
    iVar3 = extraout_r2_03;
  }
  iVar1 = DAT_000185a4;
  if (*piVar4 == param_3) {
    iVar3 = *piVar5;
  }
  if (*piVar4 == param_3 && iVar3 == param_4) {
    iVar3 = (int)*(char *)(*(int *)(DAT_0001859c + 0x18508) + 0x71c);
    if (*(int *)(DAT_000185a0 + 0x18518) == iVar3) {
      return;
    }
  }
  else {
    iVar3 = (int)*(char *)(*(int *)(DAT_0001859c + 0x18508) + 0x71c);
  }
  *(int *)(DAT_000185a0 + 0x18518) = iVar3;
  *piVar4 = param_3;
  *piVar5 = param_4;
  *(undefined1 *)(iVar1 + 0x18532) = 1;
  return;
}


// ==== FUN_000185a8 @ 000185a8

void FUN_000185a8(undefined4 param_1,undefined4 param_2,undefined4 param_3)

{
  undefined4 local_1c;
  undefined4 uStack_18;
  int local_14;
  
  local_14 = **(int **)(DAT_000185f8 + 0x185ba);
  local_1c = *(undefined4 *)(DAT_000185f4 + 0x185b8);
  uStack_18 = *(undefined4 *)(DAT_000185f4 + 0x185bc);
  FUN_00017084(&local_1c,0);
  (**(code **)(DAT_000185fc + 0x185d8))(local_1c,uStack_18,param_3);
  if (**(int **)(DAT_00018600 + 0x185e4) != local_14) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_00018604 @ 00018604

void FUN_00018604(void)

{
  float fVar1;
  float fVar2;
  float fVar3;
  float local_24;
  float local_20;
  int local_1c;
  
  local_1c = **(int **)(DAT_000186f4 + 0x18618);
  local_24 = *(float *)(DAT_000186f0 + 0x18616);
  local_20 = *(float *)(DAT_000186f0 + 0x1861a);
  FUN_00017084(&local_24,1);
  if (*(char *)(DAT_000186f8 + 0x18632) == '\x01') {
    fVar2 = *(float *)(*(int *)(DAT_000186fc + 0x186ae) + 0x98);
    fVar3 = *(float *)(*(int *)(DAT_000186fc + 0x186ae) + 0xa0);
    fVar1 = (float)FUN_0001b37c(*(undefined4 *)(DAT_00018700 + 0x18640),1,0);
    fVar3 = ABS(fVar3 - fVar2);
    fVar2 = 1.0;
    if (fVar3 < fVar1) {
      fVar2 = fVar3 / fVar1;
    }
  }
  else {
    fVar2 = 1.0;
  }
  if ((*(char *)(DAT_00018704 + 0x18684) == '\x01') &&
     (fVar1 = *(float *)(DAT_00018708 + 0x18692), fVar1 != 1.0)) {
    local_24 = fVar1 * local_24;
    local_20 = fVar1 * local_20;
    *(char *)(DAT_00018704 + 0x18684) = '\0';
  }
  FUN_00018400(0,fVar2 * local_24);
  FUN_00018410(0,fVar2 * local_20);
  if (**(int **)(DAT_0001870c + 0x186de) != local_1c) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_00018710 @ 00018710

void FUN_00018710(int param_1)

{
  undefined2 local_18;
  undefined1 local_16;
  byte local_15;
  int local_14;
  
  local_14 = **(int **)(DAT_00018768 + 0x18724);
  local_16 = *(undefined1 *)(DAT_00018760 + 0x18722);
  local_18 = *(undefined2 *)(DAT_00018760 + 0x18720);
  local_15 = *(byte *)(DAT_00018760 + 0x18723);
  if (*(byte *)(param_1 + 3) < *(byte *)(DAT_00018760 + 0x18723)) {
    local_15 = *(byte *)(param_1 + 3);
  }
  (**(code **)(DAT_00018764 + 0x18722))(&local_18);
  if (**(int **)(DAT_0001876c + 0x18750) != local_14) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_00018770 @ 00018770

void FUN_00018770(void)

{
  float fVar1;
  undefined1 uVar2;
  int iVar3;
  undefined4 *puVar4;
  int *piVar5;
  char *__s;
  size_t sVar6;
  undefined4 uVar7;
  float fVar8;
  int *piVar9;
  int iVar10;
  int iVar11;
  undefined4 uVar12;
  undefined4 *puVar13;
  
  puVar13 = (undefined4 *)(DAT_00018a10 + 0x18788);
  iVar3 = FUN_00013070(*puVar13,DAT_00018a14 + 0x1878c,0,DAT_00018a18 + 0x18790);
  puVar4 = (undefined4 *)(DAT_00018a1c + 0x187a0);
  piVar9 = (int *)(DAT_00018a20 + 0x187a6);
  piVar5 = (int *)*puVar4;
  if (iVar3 == 0) {
    (**(code **)(*piVar5 + 0x24))(piVar5,*piVar9 + 0x2bdc8e,DAT_00018a24 + 0x187c8,2);
  }
  else {
    (**(code **)(*piVar5 + 0x2c))(piVar5,*piVar9 + 0x2bdc8e,1);
  }
  iVar11 = DAT_00018a30 + 0x187d8;
  __s = (char *)FUN_00012d98(*puVar13,DAT_00018a28 + 0x187d2,DAT_00018a2c + 0x187d6);
  piVar5 = (int *)*puVar4;
  iVar3 = *piVar9;
  sVar6 = strlen(__s);
  (**(code **)(*piVar5 + 0x24))(piVar5,iVar3 + 0x2bdf18,__s,sVar6);
  uVar7 = FUN_00013168(*puVar13,DAT_00018a34 + 0x18812,0,iVar11);
  (**(code **)(*(int *)*puVar4 + 0x20))((int *)*puVar4,*piVar9 + 0x2bde2e,1);
  *(char *)(*piVar9 + 0x2bde2e) = (char)uVar7;
  (**(code **)(*(int *)*puVar4 + 0x20))((int *)*puVar4,*piVar9 + 0x2bde30,1);
  *(char *)(*piVar9 + 0x2bde30) = (char)((uint)uVar7 >> 8);
  (**(code **)(*(int *)*puVar4 + 0x20))((int *)*puVar4,*piVar9 + 0x2bde34,1);
  *(char *)(*piVar9 + 0x2bde34) = (char)((uint)uVar7 >> 0x10);
  uVar7 = FUN_00013168(*puVar13,DAT_00018a38 + 0x1888c,0xfff1cbac,iVar11);
  *(undefined4 *)(DAT_00018a3c + 0x1889a) = uVar7;
  uVar2 = FUN_00013070(*puVar13,DAT_00018a40 + 0x188a4,1,iVar11);
  *(undefined1 *)(DAT_00018a44 + 0x188b0) = uVar2;
  uVar2 = FUN_00013070(*puVar13,DAT_00018a48 + 0x188ba,1,iVar11);
  (**(code **)(*(int *)*puVar4 + 0x20))((int *)*puVar4,*piVar9 + 0x2bddec,1);
  *(undefined1 *)(*piVar9 + 0x2bddec) = uVar2;
  uVar2 = FUN_00012e04(*puVar13,DAT_00018a4c + 0x188f0,6,iVar11);
  (**(code **)(*(int *)*puVar4 + 0x20))((int *)*puVar4,*piVar9 + 0x2bde00,1);
  *(undefined1 *)(*piVar9 + 0x2bde00) = uVar2;
  uVar2 = FUN_00012e04(*puVar13,DAT_00018a50 + 0x1891e,0,iVar11);
  (**(code **)(*(int *)*puVar4 + 0x20))((int *)*puVar4,*piVar9 + 0x2bddc2,1);
  iVar3 = DAT_00018a54;
  *(undefined1 *)(*piVar9 + 0x2bddc2) = uVar2;
  uVar2 = FUN_00012e04(*puVar13,iVar3 + 0x18948,1,iVar11);
  (**(code **)(*(int *)*puVar4 + 0x20))((int *)*puVar4,*piVar9 + 0x2bde06,1);
  *(undefined1 *)(*piVar9 + 0x2bde06) = uVar2;
  uVar7 = FUN_00012f28(*puVar13,DAT_00018a58 + 0x1897c,0x3f800000,iVar11);
  iVar3 = DAT_00018a60;
  uVar12 = *puVar13;
  *(undefined4 *)(DAT_00018a5c + 0x1898a) = uVar7;
  fVar8 = (float)FUN_00012f28(uVar12,iVar3 + 0x18990,0x42b80000,iVar11);
  iVar3 = DAT_00018a64;
  fVar1 = DAT_00018a0c;
  uVar7 = *puVar13;
  iVar10 = DAT_00018a68 + 0x189bc;
  *(float *)(DAT_00018a64 + 0x189b4) = fVar8 / DAT_00018a0c;
  fVar8 = (float)FUN_00012f28(uVar7,iVar10,0x41a00000,iVar11);
  uVar7 = *puVar13;
  iVar10 = DAT_00018a6c + 0x189da;
  *(float *)(iVar3 + 0x189b8) = fVar8 / fVar1;
  uVar7 = FUN_00012f28(uVar7,iVar10,0x3f000000,iVar11);
  iVar3 = DAT_00018a70;
  uVar12 = *puVar13;
  iVar10 = DAT_00018a74 + 0x189f6;
  *(undefined4 *)(DAT_00018a70 + 0x189f4) = uVar7;
  uVar7 = FUN_00012f28(uVar12,iVar10,0x3fa66666,iVar11);
  *(undefined4 *)(iVar3 + 0x189f8) = uVar7;
  return;
}


// ==== FUN_00018a78 @ 00018a78

void FUN_00018a78(void)

{
  undefined4 *puVar1;
  int *piVar2;
  
  puVar1 = (undefined4 *)(DAT_00018b30 + 0x18a90);
  piVar2 = (int *)(DAT_00018b34 + 0x18a92);
  (**(code **)(*(int *)*puVar1 + 0xc0))
            ((int *)*puVar1,*piVar2 + 0x2bde29,DAT_00018b38 + 0x18a9c,DAT_00018b3c + 0x18aaa);
  (**(code **)(*(int *)*puVar1 + 0xc0))
            ((int *)*puVar1,*piVar2 + 0x2bde63,DAT_00018b40 + 0x18ac2,DAT_00018b44 + 0x18ac4);
  (**(code **)(*(int *)*puVar1 + 0x30))((int *)*puVar1,*piVar2 + 0x2bde66,*piVar2 + 0x2bdea6);
  (**(code **)(*(int *)*puVar1 + 0xc0))
            ((int *)*puVar1,*piVar2 + 0x2bdefb,DAT_00018b48 + 0x18af4,DAT_00018b4c + 0x18af6);
  (**(code **)(*(int *)*puVar1 + 0xbc))
            ((int *)*puVar1,*piVar2 + 0x2bdd91,DAT_00018b50 + 0x18b0c,DAT_00018b54 + 0x18b0e);
                    /* WARNING: Could not recover jumptable at 0x00018b2c. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (**(code **)(*(int *)*puVar1 + 0xbc))
            ((int *)*puVar1,*piVar2 + 0x2bdd9f,DAT_00018b58 + 0x18b20,DAT_00018b5c + 0x18b26);
  return;
}


// ==== _INIT_6 @ 00018b60

void _INIT_6(void)

{
  undefined4 uVar1;
  
  uVar1 = FUN_000126ac(DAT_00018b74 + 0x18b6a);
  *(undefined4 *)(DAT_00018b78 + 0x18b72) = uVar1;
  return;
}


// ==== FUN_00018b7c @ 00018b7c

void FUN_00018b7c(undefined4 param_1,undefined4 param_2,undefined4 param_3)

{
  undefined4 local_1c;
  undefined4 uStack_18;
  int local_14;
  
  local_14 = **(int **)(DAT_00018bcc + 0x18b8e);
  local_1c = *(undefined4 *)(DAT_00018bc8 + 0x18b8c);
  uStack_18 = *(undefined4 *)(DAT_00018bc8 + 0x18b90);
  FUN_00017084(&local_1c,0);
  (**(code **)(DAT_00018bd0 + 0x18bac))(local_1c,uStack_18,param_3);
  if (**(int **)(DAT_00018bd4 + 0x18bb8) != local_14) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_00018bd8 @ 00018bd8

void FUN_00018bd8(void)

{
  float fVar1;
  int iVar2;
  undefined1 uVar3;
  undefined4 uVar4;
  float fVar5;
  int iVar6;
  undefined4 *puVar7;
  undefined4 *puVar8;
  int iVar9;
  int *piVar10;
  
  puVar7 = (undefined4 *)(DAT_00018d5c + 0x18bee);
  iVar9 = DAT_00018d64 + 0x18bf6;
  uVar4 = FUN_00013168(*puVar7,DAT_00018d60 + 0x18bf2,0);
  piVar10 = (int *)(DAT_00018d68 + 0x18c0a);
  puVar8 = (undefined4 *)(DAT_00018d6c + 0x18c1a);
  (**(code **)(*(int *)*puVar8 + 0x20))((int *)*puVar8,*piVar10 + 0x4383a0,1);
  *(char *)(*piVar10 + 0x4383a0) = (char)uVar4;
  (**(code **)(*(int *)*puVar8 + 0x20))((int *)*puVar8,*piVar10 + 0x4383a2,1);
  *(char *)(*piVar10 + 0x4383a2) = (char)((uint)uVar4 >> 8);
  (**(code **)(*(int *)*puVar8 + 0x20))((int *)*puVar8,*piVar10 + 0x4383a4,1);
  *(char *)(*piVar10 + 0x4383a4) = (char)((uint)uVar4 >> 0x10);
  uVar3 = FUN_00013070(*puVar7,DAT_00018d70 + 0x18c6a,1,iVar9);
  (**(code **)(*(int *)*puVar8 + 0x20))((int *)*puVar8,*piVar10 + 0x438336,1);
  *(undefined1 *)(*piVar10 + 0x438336) = uVar3;
  uVar3 = FUN_00012e04(*puVar7,DAT_00018d74 + 0x18c94,0,iVar9);
  (**(code **)(*(int *)*puVar8 + 0x20))((int *)*puVar8,*piVar10 + 0x4383b4,1);
  *(undefined1 *)(*piVar10 + 0x4383b4) = uVar3;
  uVar3 = FUN_00012e04(*puVar7,DAT_00018d78 + 0x18cbe,2,iVar9);
  (**(code **)(*(int *)*puVar8 + 0x20))((int *)*puVar8,*piVar10 + 0x438372,1);
  *(undefined1 *)(*piVar10 + 0x438372) = uVar3;
  uVar3 = FUN_00012e04(*puVar7,DAT_00018d7c + 0x18ce8,2,iVar9);
  (**(code **)(*(int *)*puVar8 + 0x20))((int *)*puVar8,*piVar10 + 0x43836c,1);
  *(undefined1 *)(*piVar10 + 0x43836c) = uVar3;
  fVar5 = (float)FUN_00012f28(*puVar7,DAT_00018d80 + 0x18d16,0x42c40000,iVar9);
  iVar2 = DAT_00018d84;
  fVar1 = DAT_00018d58;
  uVar4 = *puVar7;
  iVar6 = DAT_00018d88 + 0x18d3a;
  *(float *)(DAT_00018d84 + 0x18d32) = fVar5 / DAT_00018d58;
  fVar5 = (float)FUN_00012f28(uVar4,iVar6,0x42bc0000,iVar9);
  *(float *)(iVar2 + 0x18d36) = fVar5 / fVar1;
  return;
}


// ==== FUN_00018d8c @ 00018d8c

void FUN_00018d8c(void)

{
                    /* WARNING: Could not recover jumptable at 0x00018dba. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (**(code **)(**(int **)(DAT_00018dbc + 0x18da0) + 0xc0))
            (*(int **)(DAT_00018dbc + 0x18da0),*(int *)(DAT_00018dc0 + 0x18da2) + 0x43847d,
             DAT_00018dc4 + 0x18dac,DAT_00018dc8 + 0x18db0);
  return;
}


// ==== FUN_00018dcc @ 00018dcc

void FUN_00018dcc(int param_1)

{
  undefined2 local_18;
  undefined1 local_16;
  byte local_15;
  int local_14;
  
  local_14 = **(int **)(DAT_00018e24 + 0x18de0);
  local_16 = *(undefined1 *)(DAT_00018e1c + 0x18dde);
  local_18 = *(undefined2 *)(DAT_00018e1c + 0x18ddc);
  local_15 = *(byte *)(DAT_00018e1c + 0x18ddf);
  if (*(byte *)(param_1 + 3) < *(byte *)(DAT_00018e1c + 0x18ddf)) {
    local_15 = *(byte *)(param_1 + 3);
  }
  (**(code **)(DAT_00018e20 + 0x18dde))(&local_18);
  if (**(int **)(DAT_00018e28 + 0x18e0c) != local_14) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_00018e2c @ 00018e2c

void FUN_00018e2c(void)

{
  undefined4 local_14;
  undefined4 local_10;
  int local_c;
  
  local_c = **(int **)(DAT_00018e7c + 0x18e3c);
  local_14 = *(undefined4 *)(DAT_00018e78 + 0x18e3a);
  local_10 = *(undefined4 *)(DAT_00018e78 + 0x18e3e);
  FUN_00017084(&local_14,1);
  FUN_00018400(0,local_14);
  FUN_00018410(0,local_10);
  if (**(int **)(DAT_00018e80 + 0x18e68) != local_c) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_00018e84 @ 00018e84

void FUN_00018e84(undefined4 param_1,undefined4 param_2,undefined4 param_3)

{
  undefined4 local_1c;
  undefined4 uStack_18;
  int local_14;
  
  local_14 = **(int **)(DAT_00018ed4 + 0x18e96);
  local_1c = *(undefined4 *)(DAT_00018ed0 + 0x18e94);
  uStack_18 = *(undefined4 *)(DAT_00018ed0 + 0x18e98);
  FUN_00017084(&local_1c,0);
  (**(code **)(DAT_00018ed8 + 0x18eb4))(local_1c,uStack_18,param_3);
  if (**(int **)(DAT_00018edc + 0x18ec0) != local_14) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_00018ee0 @ 00018ee0

void FUN_00018ee0(void)

{
  float fVar1;
  undefined1 uVar2;
  char *__s;
  undefined4 *puVar3;
  size_t sVar4;
  undefined4 uVar5;
  float fVar6;
  int iVar7;
  int iVar8;
  undefined4 uVar9;
  undefined4 *puVar10;
  int *piVar11;
  int *piVar12;
  int iVar13;
  
  puVar10 = (undefined4 *)(DAT_00019118 + 0x18ef6);
  iVar8 = DAT_00019124 + 0x18f02;
  __s = (char *)FUN_00012d98(*puVar10,DAT_0001911c + 0x18efc,DAT_00019120 + 0x18f00);
  puVar3 = (undefined4 *)(DAT_00019128 + 0x18f12);
  piVar11 = (int *)(DAT_0001912c + 0x18f14);
  piVar12 = (int *)*puVar3;
  iVar13 = *piVar11;
  sVar4 = strlen(__s);
  (**(code **)(*piVar12 + 0x24))(piVar12,iVar13 + 0x2bd5a8,__s,sVar4);
  iVar13 = *piVar11;
  piVar12 = (int *)*puVar3;
  sVar4 = strlen(__s);
  (**(code **)(*piVar12 + 0x24))(piVar12,iVar13 + 0x60fee4,__s,sVar4);
  uVar5 = FUN_00013168(*puVar10,DAT_00019130 + 0x18f70,0,iVar8);
  (**(code **)(*(int *)*puVar3 + 0x20))((int *)*puVar3,*piVar11 + 0x2bd0e6,1);
  *(char *)(*piVar11 + 0x2bd0e6) = (char)uVar5;
  (**(code **)(*(int *)*puVar3 + 0x20))((int *)*puVar3,*piVar11 + 0x2bd0ec,1);
  *(char *)(*piVar11 + 0x2bd0ec) = (char)((uint)uVar5 >> 8);
  (**(code **)(*(int *)*puVar3 + 0x20))((int *)*puVar3,*piVar11 + 0x2bd0ee,1);
  *(char *)(*piVar11 + 0x2bd0ee) = (char)((uint)uVar5 >> 0x10);
  uVar5 = FUN_00013168(*puVar10,DAT_00019134 + 0x18fdc,0xffe1e1e1,iVar8);
  *(undefined4 *)(DAT_00019138 + 0x18fe8) = uVar5;
  uVar2 = FUN_00013070(*puVar10,DAT_0001913c + 0x18ff2,0,iVar8);
  (**(code **)(*(int *)*puVar3 + 0x20))((int *)*puVar3,*piVar11 + 0x2bd08a,1);
  *(undefined1 *)(*piVar11 + 0x2bd08a) = uVar2;
  uVar2 = FUN_00012e04(*puVar10,DAT_00019140 + 0x1901c,2,iVar8);
  (**(code **)(*(int *)*puVar3 + 0x20))((int *)*puVar3,*piVar11 + 0x2bd0a8,1);
  *(undefined1 *)(*piVar11 + 0x2bd0a8) = uVar2;
  uVar2 = FUN_00012e04(*puVar10,DAT_00019144 + 0x19046,5,iVar8);
  (**(code **)(*(int *)*puVar3 + 0x20))((int *)*puVar3,*piVar11 + 0x2bd0a2,1);
  iVar13 = DAT_00019148;
  *(undefined1 *)(*piVar11 + 0x2bd0a2) = uVar2;
  uVar2 = FUN_00012e04(*puVar10,iVar13 + 0x1906c,1,iVar8);
  (**(code **)(*(int *)*puVar3 + 0x20))((int *)*puVar3,*piVar11 + 0x2bd0b4,1);
  *(undefined1 *)(*piVar11 + 0x2bd0b4) = uVar2;
  fVar6 = (float)FUN_00012f28(*puVar10,DAT_0001914c + 0x1909e,0x42ac0000,iVar8);
  iVar13 = DAT_00019150;
  fVar1 = DAT_00019114;
  uVar5 = *puVar10;
  iVar7 = DAT_00019154 + 0x190c0;
  *(float *)(DAT_00019150 + 0x190bc) = fVar6 / DAT_00019114;
  fVar6 = (float)FUN_00012f28(uVar5,iVar7,0x40800000,iVar8);
  uVar5 = *puVar10;
  iVar7 = DAT_00019158 + 0x190e0;
  *(float *)(iVar13 + 0x190c0) = fVar6 / fVar1;
  uVar5 = FUN_00012f28(uVar5,iVar7,0x3ef0a3d7,iVar8);
  iVar13 = DAT_0001915c;
  uVar9 = *puVar10;
  iVar7 = DAT_00019160 + 0x190fe;
  *(undefined4 *)(DAT_0001915c + 0x190fc) = uVar5;
  uVar5 = FUN_00012f28(uVar9,iVar7,0x3fb33333,iVar8);
  *(undefined4 *)(iVar13 + 0x19100) = uVar5;
  return;
}


// ==== FUN_00019164 @ 00019164

void FUN_00019164(void)

{
  undefined4 *puVar1;
  int *piVar2;
  
  puVar1 = (undefined4 *)(DAT_000191d0 + 0x1917c);
  piVar2 = (int *)(DAT_000191d4 + 0x1917e);
  (**(code **)(*(int *)*puVar1 + 0xc0))
            ((int *)*puVar1,*piVar2 + 0x2bd111,DAT_000191d8 + 0x19188,DAT_000191dc + 0x19196);
  (**(code **)(*(int *)*puVar1 + 0xc0))
            ((int *)*puVar1,*piVar2 + 0x2bd0df,DAT_000191e0 + 0x191ac,DAT_000191e4 + 0x191ae);
                    /* WARNING: Could not recover jumptable at 0x000191cc. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (**(code **)(*(int *)*puVar1 + 0xc0))
            ((int *)*puVar1,*piVar2 + 0x2bd18d,DAT_000191e8 + 0x191c0,DAT_000191ec + 0x191c6);
  return;
}


// ==== FUN_000191f0 @ 000191f0

void FUN_000191f0(void)

{
  return;
}


// ==== FUN_000191f2 @ 000191f2

void FUN_000191f2(void)

{
  return;
}


// ==== FUN_000191f4 @ 000191f4

void FUN_000191f4(int param_1)

{
  undefined1 *puVar1;
  undefined1 *puVar2;
  byte *pbVar3;
  int iVar4;
  undefined1 local_18;
  undefined1 local_17;
  undefined1 local_16;
  byte local_15;
  int local_14;
  
  local_14 = **(int **)(DAT_00019290 + 0x19208);
  puVar1 = (undefined1 *)(DAT_00019294 + 0x19223);
  pbVar3 = (byte *)(DAT_00019294 + 0x19225);
  iVar4 = *(int *)((short)(ushort)**(byte **)(DAT_00019288 + 0x19204) * 0x194 +
                   *(int *)(DAT_0001928c + 0x19206) + 0xbc);
  puVar2 = (undefined1 *)(DAT_00019294 + 0x19224);
  if (iVar4 < 0) {
    puVar2 = (undefined1 *)(DAT_00019298 + 0x1922c);
    puVar1 = (undefined1 *)(DAT_00019298 + 0x1922b);
  }
  local_16 = *puVar2;
  local_17 = *puVar1;
  if (iVar4 < 0) {
    pbVar3 = (byte *)(DAT_00019298 + 0x1922d);
  }
  puVar2 = (undefined1 *)(DAT_00019294 + 0x19222);
  if (iVar4 < 0) {
    puVar2 = (undefined1 *)(DAT_00019298 + 0x1922a);
  }
  local_18 = *puVar2;
  local_15 = *(byte *)(param_1 + 3);
  if (*pbVar3 < *(byte *)(param_1 + 3)) {
    local_15 = *pbVar3;
  }
  (**(code **)(DAT_0001929c + 0x19258))(&local_18);
  if (**(int **)(DAT_000192a0 + 0x19278) != local_14) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_000192a4 @ 000192a4

void FUN_000192a4(void)

{
  undefined4 local_14;
  undefined4 local_10;
  int local_c;
  
  local_c = **(int **)(DAT_000192f4 + 0x192b4);
  local_14 = *(undefined4 *)(DAT_000192f0 + 0x192b2);
  local_10 = *(undefined4 *)(DAT_000192f0 + 0x192b6);
  FUN_00017084(&local_14,1);
  FUN_00018400(0,local_14);
  FUN_00018410(0,local_10);
  if (**(int **)(DAT_000192f8 + 0x192e0) != local_c) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_000192fc @ 000192fc

void FUN_000192fc(undefined4 param_1,int param_2)

{
  if (*(char *)(param_2 + 2) == 'd') {
    param_2 = DAT_0001931c + 0x1930c;
  }
                    /* WARNING: Could not recover jumptable at 0x00019316. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (**(code **)(DAT_00019318 + 0x1930a))(param_1,param_2);
  return;
}


// ==== FUN_00019320 @ 00019320

void FUN_00019320(undefined4 param_1,undefined4 param_2,undefined4 param_3)

{
  char cVar1;
  int iVar2;
  float fVar3;
  float *pfVar4;
  float local_3c;
  float local_38;
  float local_34;
  float local_30;
  undefined4 local_2c;
  float local_28;
  undefined4 local_24;
  float local_20;
  int local_1c;
  
  local_1c = **(int **)(DAT_0001940c + 0x19338);
  local_24 = *(undefined4 *)(DAT_00019408 + 0x19336);
  local_20 = *(float *)(DAT_00019408 + 0x1933a);
  FUN_00017084(&local_24,0);
  local_2c = *(undefined4 *)(DAT_00019410 + 0x19352);
  local_28 = *(float *)(DAT_00019410 + 0x19356);
  FUN_00017084(&local_2c,1);
  iVar2 = DAT_00019414;
  local_38 = *(float *)(DAT_00019414 + 0x193ee);
  local_34 = local_38 + *(float *)(DAT_00019414 + 0x193e2);
  local_30 = *(float *)(DAT_00019414 + 0x193e6) - local_38;
  local_38 = *(float *)(DAT_00019414 + 0x193e6) + local_38;
  cVar1 = *(char *)(DAT_00019414 + 0x193db);
  local_3c = *(float *)(DAT_00019414 + 0x193e2) - *(float *)(DAT_00019414 + 0x193ea);
  pfVar4 = (float *)(DAT_00019418 + 0x1938e);
  *pfVar4 = 0.0;
  if (((cVar1 == '\x01') && (*(char *)(*(int *)(iVar2 + 0x193d6) + 0xb4) == '\0')) &&
     (fVar3 = (float)FUN_00017868(&local_3c,iVar2 + 0x193e2,local_28,0x3f000000),
     local_20 - fVar3 < local_28)) {
    *pfVar4 = fVar3 - local_20;
    local_20 = fVar3;
  }
  (**(code **)(DAT_0001941c + 0x193ea))(local_24,local_20,param_3);
  if (**(int **)(DAT_00019420 + 0x193f6) != local_1c) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_000197d0 @ 000197d0

void FUN_000197d0(undefined4 param_1,undefined4 param_2,undefined4 param_3,undefined4 param_4)

{
  int iVar1;
  undefined4 local_4;
  
  iVar1 = **(int **)(DAT_0001981c + 0x197ea);
  local_4 = param_4;
  __vsprintf_chk(DAT_00019818 + 0x197e6,0,0xff,param_3,&local_4,&local_4);
  if (**(int **)(DAT_00019820 + 0x19804) != iVar1) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_00019824 @ 00019824

void FUN_00019824(void)

{
  float fVar1;
  int iVar2;
  int iVar3;
  undefined1 uVar4;
  undefined2 uVar5;
  undefined8 *puVar6;
  undefined4 uVar7;
  int iVar8;
  int *piVar9;
  float fVar10;
  int iVar11;
  int iVar12;
  int iVar13;
  int iVar14;
  int iVar15;
  int iVar16;
  int iVar17;
  int iVar18;
  undefined4 *puVar19;
  int iVar20;
  undefined4 uVar21;
  code *pcVar22;
  undefined4 *puVar23;
  int *piVar24;
  int iVar25;
  float *pfVar26;
  int iVar27;
  float fVar28;
  undefined8 uVar29;
  undefined8 uVar30;
  undefined8 uVar31;
  
  puVar23 = (undefined4 *)(DAT_00019c24 + 0x1983c);
  iVar20 = DAT_00019c30 + 0x19848;
  puVar6 = (undefined8 *)FUN_00012d98(*puVar23,DAT_00019c28 + 0x19842,DAT_00019c2c + 0x19846);
  iVar3 = DAT_00019c50;
  iVar2 = DAT_00019c4c;
  uVar29 = puVar6[1];
  uVar30 = puVar6[2];
  uVar31 = puVar6[3];
  uVar7 = *puVar23;
  *(undefined8 *)(DAT_00019c4c + 0x1985e) = *puVar6;
  *(undefined8 *)(iVar2 + 0x19866) = uVar29;
  *(undefined8 *)(iVar2 + 0x1986e) = uVar30;
  *(undefined8 *)(iVar2 + 0x19876) = uVar31;
  uVar7 = FUN_00013168(uVar7,iVar3 + 0x19868,0xff1d19b4,iVar20);
  iVar2 = DAT_00019c58;
  uVar21 = *puVar23;
  *(undefined4 *)(DAT_00019c54 + 0x19882) = uVar7;
  uVar7 = FUN_00013168(uVar21,iVar2 + 0x1988a,0xff2c6836,iVar20);
  uVar21 = *puVar23;
  *(undefined4 *)(DAT_00019c5c + 0x1989e) = uVar7;
  iVar11 = DAT_00019c60 + 0x198aa;
  uVar7 = FUN_00013168(uVar21,iVar11,0,iVar20);
  iVar25 = DAT_00019c6c;
  piVar24 = (int *)(DAT_00019c64 + 0x198ba);
  iVar8 = *piVar24;
  puVar19 = (undefined4 *)(DAT_00019c68 + 0x198ca);
  piVar9 = (int *)*puVar19;
  pcVar22 = *(code **)(*piVar9 + 0x20);
  *(undefined4 *)(DAT_00019c6c + 0x198d8) = uVar7;
  (*pcVar22)(piVar9,iVar8 + 0x2bd1ca,1);
  *(char *)(*piVar24 + 0x2bd1ca) = (char)uVar7;
  uVar4 = *(undefined1 *)(iVar25 + 0x198d9);
  (**(code **)(*(int *)*puVar19 + 0x20))((int *)*puVar19,*piVar24 + 0x2bd1d0,1);
  *(undefined1 *)(*piVar24 + 0x2bd1d0) = uVar4;
  uVar4 = *(undefined1 *)(iVar25 + 0x198da);
  (**(code **)(*(int *)*puVar19 + 0x20))((int *)*puVar19,*piVar24 + 0x2bd1d2,1);
  *(undefined1 *)(*piVar24 + 0x2bd1d2) = uVar4;
  iVar8 = DAT_00019c70 + 0x19938;
  uVar4 = FUN_00012e04(*puVar23,iVar8,5,iVar20);
  (**(code **)(*(int *)*puVar19 + 0x20))((int *)*puVar19,*piVar24 + 0x2bd1ba,1);
  *(undefined1 *)(*piVar24 + 0x2bd1ba) = uVar4;
  iVar12 = DAT_00019c74 + 0x19964;
  uVar4 = FUN_00013070(*puVar23,iVar12,0,iVar20);
  (**(code **)(*(int *)*puVar19 + 0x20))((int *)*puVar19,*piVar24 + 0x2bd190,1);
  *(undefined1 *)(*piVar24 + 0x2bd190) = uVar4;
  iVar13 = DAT_00019c78 + 0x19990;
  uVar4 = FUN_00012e04(*puVar23,iVar13,2,iVar20);
  (**(code **)(*(int *)*puVar19 + 0x20))((int *)*puVar19,*piVar24 + 0x2bd19e,1);
  *(undefined1 *)(*piVar24 + 0x2bd19e) = uVar4;
  iVar14 = DAT_00019c7c + 0x199bc;
  uVar4 = FUN_00012e04(*puVar23,iVar14,1,iVar20);
  (**(code **)(*(int *)*puVar19 + 0x20))((int *)*puVar19,*piVar24 + 0x2bd1dc,1);
  *(undefined1 *)(*piVar24 + 0x2bd1dc) = uVar4;
  iVar15 = DAT_00019c80 + 0x199e8;
  uVar4 = FUN_00012e04(*puVar23,iVar15,0,iVar20);
  (**(code **)(*(int *)*puVar19 + 0x20))((int *)*puVar19,*piVar24 + 0x2bd1c0,1);
  *(undefined1 *)(*piVar24 + 0x2bd1c0) = uVar4;
  iVar27 = DAT_00019c84 + 0x19a18;
  fVar10 = (float)FUN_00012f28(*puVar23,iVar27,0x42ac0000,iVar20);
  iVar25 = DAT_00019c88;
  fVar1 = DAT_00019c48;
  uVar7 = *puVar23;
  iVar16 = DAT_00019c8c + 0x19a3e;
  *(float *)(DAT_00019c88 + 0x19a36) = fVar10 / DAT_00019c48;
  fVar10 = (float)FUN_00012f28(uVar7,iVar16,0x41300000,iVar20);
  uVar7 = *puVar23;
  *(float *)(iVar25 + 0x19a3a) = fVar10 / fVar1;
  iVar17 = DAT_00019c90 + 0x19a68;
  uVar7 = FUN_00012f28(uVar7,iVar17,0x3ef0a3d7,iVar20);
  iVar25 = DAT_00019c94;
  *(undefined4 *)(DAT_00019c94 + 0x19a7c) = uVar7;
  iVar18 = DAT_00019c98 + 0x19a86;
  uVar7 = FUN_00012f28(*puVar23,iVar18,0x3f8ccccd,iVar20);
  *(undefined4 *)(iVar25 + 0x19a80) = uVar7;
  iVar25 = DAT_00019ca0 + 0x19a9c;
  uVar4 = FUN_00013070(*puVar23,DAT_00019c9c + 0x19a9a,0,iVar25);
  *(undefined1 *)(DAT_00019ca4 + 0x19aaa) = uVar4;
  uVar4 = FUN_00013070(*puVar23,iVar12,1,iVar25);
  *(undefined1 *)(DAT_00019ca8 + 0x19abe) = uVar4;
  uVar5 = FUN_00012e04(*puVar23,iVar14,1,iVar25);
  *(undefined2 *)(DAT_00019cac + 0x19ad2) = uVar5;
  uVar5 = FUN_00012e04(*puVar23,iVar13,0,iVar25);
  *(undefined2 *)(DAT_00019cb0 + 0x19ae6) = uVar5;
  uVar5 = FUN_00012e04(*puVar23,iVar8,5,iVar25);
  *(undefined2 *)(DAT_00019cb4 + 0x19afa) = uVar5;
  uVar5 = FUN_00012e04(*puVar23,iVar15,0,iVar25);
  *(undefined2 *)(DAT_00019cb8 + 0x19b10) = uVar5;
  uVar7 = FUN_00012e04(*puVar23,DAT_00019cbc + 0x19b1a,3000,iVar25);
  *(undefined4 *)(DAT_00019cc0 + 0x19b28) = uVar7;
  fVar10 = (float)FUN_00012f28(*puVar23,DAT_00019cc4 + 0x19b32,0x3f000000,iVar25);
  pfVar26 = (float *)(DAT_00019cc8 + 0x19b3e);
  *pfVar26 = fVar10;
  uVar7 = FUN_00012f28(*puVar23,DAT_00019ccc + 0x19b48,0,iVar25);
  fVar28 = *pfVar26;
  *(undefined4 *)(DAT_00019cd0 + 0x19b5c) = uVar7;
  fVar10 = 1.0;
  if (fVar28 < 1.0) {
    fVar10 = fVar28;
  }
  uVar7 = *puVar23;
  *pfVar26 = fVar10;
  uVar7 = FUN_00013168(uVar7,iVar3 + 0x19868,0xff1d19b4,iVar25);
  *(undefined4 *)(DAT_00019cd4 + 0x19b8e) = uVar7;
  uVar7 = FUN_00013168(*puVar23,iVar2 + 0x1988a,0xff2c6836,iVar25);
  *(undefined4 *)(DAT_00019cd8 + 0x19ba4) = uVar7;
  uVar7 = FUN_00013168(*puVar23,iVar11,0xff000000,iVar25);
  *(undefined4 *)(DAT_00019cdc + 0x19bbc) = uVar7;
  fVar10 = (float)FUN_00012f28(*puVar23,iVar27,0x42480000,iVar25);
  iVar2 = DAT_00019ce0;
  uVar7 = *puVar23;
  *(float *)(DAT_00019ce0 + 0x19bde) = fVar10 / fVar1;
  fVar10 = (float)FUN_00012f28(uVar7,iVar16,0x42480000,iVar25);
  uVar7 = *puVar23;
  *(float *)(iVar2 + 0x19be2) = fVar10 / fVar1;
  uVar7 = FUN_00012f28(uVar7,iVar17,0x3ef0a3d7,iVar25);
  iVar2 = DAT_00019ce4;
  uVar21 = *puVar23;
  *(undefined4 *)(DAT_00019ce4 + 0x19c18) = uVar7;
  uVar7 = FUN_00012f28(uVar21,iVar18,0x3f8ccccd,iVar25);
  *(undefined4 *)(iVar2 + 0x19c1c) = uVar7;
  return;
}


// ==== FUN_00019ce8 @ 00019ce8

void FUN_00019ce8(void)

{
  int iVar1;
  int *piVar2;
  int iVar3;
  undefined4 *puVar4;
  
  puVar4 = (undefined4 *)(DAT_00019d88 + 0x19d00);
  piVar2 = (int *)(DAT_00019d8c + 0x19d02);
  (**(code **)(*(int *)*puVar4 + 0xc0))
            ((int *)*puVar4,*piVar2 + 0x2bd2cd,DAT_00019d90 + 0x19d0c,DAT_00019d94 + 0x19d1a);
  (**(code **)(*(int *)*puVar4 + 0xc0))
            ((int *)*puVar4,*piVar2 + 0x2bd267,DAT_00019d98 + 0x19d32,DAT_00019d9c + 0x19d34);
  (**(code **)(*(int *)*puVar4 + 0xc0))
            ((int *)*puVar4,*piVar2 + 0x2bd241,DAT_00019da0 + 0x19d4c,DAT_00019da4 + 0x19d4e);
  iVar1 = DAT_00019da8 + 0x19d5e;
  iVar3 = DAT_00019dac + 0x19d68;
  (**(code **)(*(int *)*puVar4 + 0xc0))((int *)*puVar4,*piVar2 + 0x2bd1ff,iVar1,iVar3);
                    /* WARNING: Could not recover jumptable at 0x00019d84. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (**(code **)(*(int *)*puVar4 + 0xc0))((int *)*puVar4,*piVar2 + 0x2bd2a5,iVar1,iVar3);
  return;
}


// ==== FUN_00019db0 @ 00019db0

void FUN_00019db0(float *param_1,undefined4 param_2)

{
  int iVar1;
  int iVar2;
  float *pfVar3;
  float *pfVar4;
  code *pcVar5;
  float fVar6;
  float fVar7;
  float fVar8;
  
  iVar1 = DAT_00019e74;
  fVar7 = *(float *)(DAT_00019e74 + 0x19dca);
  fVar6 = *(float *)(DAT_00019e74 + 0x19dce);
  iVar2 = FUN_000127cc(0xffffffff,1);
  if ((iVar2 != 0) && (*(int *)(iVar2 + 0x5a4) == 4)) {
    fVar7 = DAT_00019e6c;
    fVar6 = DAT_00019e6c;
  }
  fVar8 = *(float *)(iVar1 + 0x19dd2) + **(float **)(DAT_00019e78 + 0x19df6);
  pcVar5 = *(code **)(DAT_00019e7c + 0x19e04);
  **(float **)(DAT_00019e78 + 0x19df6) = fVar8;
  (*pcVar5)(param_1,param_2);
  fVar8 = fVar8 / DAT_00019e70;
  pfVar3 = *(float **)(DAT_00019e80 + 0x19e1e);
  pfVar4 = *(float **)(DAT_00019e84 + 0x19e20);
  fVar7 = fVar7 * fVar8;
  fVar6 = fVar6 * fVar8;
  *param_1 = *param_1 - (*pfVar3 * fVar7 - *pfVar4 * fVar6);
  param_1[1] = param_1[1] - (fVar6 * *pfVar3 + *pfVar4 * fVar7);
  return;
}


// ==== FUN_00019e88 @ 00019e88

void FUN_00019e88(int param_1)

{
  int iVar1;
  undefined8 uVar2;
  undefined4 local_34;
  undefined4 uStack_30;
  undefined4 local_2c;
  undefined4 uStack_28;
  undefined4 local_24;
  undefined4 uStack_20;
  undefined4 local_1c;
  undefined4 uStack_18;
  int local_14;
  
  local_14 = **(int **)(DAT_00019f1c + 0x19e9c);
  (**(code **)(DAT_00019f18 + 0x19e96))(param_1);
  local_24 = *(undefined4 *)(DAT_00019f20 + 0x19eac);
  uStack_20 = *(undefined4 *)(DAT_00019f20 + 0x19eb0);
  local_1c = *(undefined4 *)(DAT_00019f24 + 0x19eae);
  uStack_18 = *(undefined4 *)(DAT_00019f24 + 0x19eb2);
  FUN_00017084(&local_1c,0);
  FUN_00017084(&local_24,0);
  local_2c = local_1c;
  uStack_28 = uStack_18;
  local_34 = local_24;
  uStack_30 = uStack_20;
  iVar1 = FUN_000182b0(param_1,&local_2c,&local_34);
  if (param_1 != 0) {
    iVar1 = *(int *)(param_1 + 0x88);
  }
  if (param_1 != 0 && iVar1 != 0) {
    uVar2 = *(undefined8 *)(param_1 + 0x28);
    *(undefined8 *)(iVar1 + 0xc) = *(undefined8 *)(param_1 + 0x20);
    *(undefined8 *)(iVar1 + 0x14) = uVar2;
  }
  if (**(int **)(DAT_00019f28 + 0x19f08) != local_14) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_00019f2c @ 00019f2c

void FUN_00019f2c(void)

{
  int iVar1;
  undefined4 uVar2;
  float fVar3;
  float fVar4;
  int iVar5;
  undefined4 uVar6;
  int iVar7;
  undefined4 *puVar8;
  
  puVar8 = (undefined4 *)(DAT_0001a03c + 0x19f44);
  iVar7 = DAT_0001a044 + 0x19f48;
  uVar2 = FUN_00012f28(*puVar8,DAT_0001a040 + 0x19f46,0,iVar7);
  iVar1 = DAT_0001a048;
  uVar6 = *puVar8;
  iVar5 = DAT_0001a04c + 0x19f5c;
  *(undefined4 *)(DAT_0001a048 + 0x19f5a) = uVar2;
  uVar2 = FUN_00012f28(uVar6,iVar5,0,iVar7);
  uVar6 = *puVar8;
  iVar5 = DAT_0001a050 + 0x19f6e;
  *(undefined4 *)(iVar1 + 0x19f5e) = uVar2;
  fVar3 = (float)FUN_00012f28(uVar6,iVar5,0,iVar7);
  iVar5 = DAT_0001a054;
  if (DAT_0001a030 < fVar3) {
    fVar3 = DAT_0001a030;
  }
  fVar4 = DAT_0001a034;
  if (DAT_0001a034 < fVar3) {
    fVar4 = fVar3;
  }
  uVar2 = *puVar8;
  *(float *)(iVar1 + 0x19f62) = fVar4;
  fVar4 = (float)FUN_00012f28(uVar2,iVar5 + 0x19fb2,0x40e00000,iVar7);
  iVar1 = DAT_0001a058;
  fVar3 = DAT_0001a038;
  uVar2 = *puVar8;
  iVar5 = DAT_0001a05c + 0x19fd4;
  *(float *)(DAT_0001a058 + 0x19fcc) = fVar4 / DAT_0001a038;
  fVar4 = (float)FUN_00012f28(uVar2,iVar5,0x41600000,iVar7);
  uVar2 = *puVar8;
  iVar5 = DAT_0001a060 + 0x19ff0;
  *(float *)(iVar1 + 0x19fd0) = fVar4 / fVar3;
  fVar4 = (float)FUN_00012f28(uVar2,iVar5,0x40b00000,iVar7);
  iVar1 = DAT_0001a064;
  uVar2 = *puVar8;
  iVar5 = DAT_0001a068 + 0x1a012;
  *(float *)(DAT_0001a064 + 0x1a00c) = fVar4 / fVar3;
  fVar4 = (float)FUN_00012f28(uVar2,iVar5,0x41480000,iVar7);
  *(float *)(iVar1 + 0x1a010) = fVar4 / fVar3;
  return;
}


// ==== FUN_0001a06c @ 0001a06c

void FUN_0001a06c(void)

{
  int *piVar1;
  undefined4 uVar2;
  undefined4 *puVar3;
  
  puVar3 = (undefined4 *)(DAT_0001a0bc + 0x1a07e);
  piVar1 = (int *)*puVar3;
  (**(code **)(*piVar1 + 0xc0))
            (piVar1,*(int *)(DAT_0001a0c0 + 0x1a088) + 0x43e25d,DAT_0001a0c4 + 0x1a090,
             DAT_0001a0c8 + 0x1a096);
  piVar1 = (int *)*puVar3;
  uVar2 = FUN_000126ac(DAT_0001a0cc + 0x1a0a0);
                    /* WARNING: Could not recover jumptable at 0x0001a0b8. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (**(code **)(*piVar1 + 0x18))(piVar1,uVar2,DAT_0001a0d0 + 0x1a0b2,DAT_0001a0d4 + 0x1a0b4);
  return;
}


// ==== FUN_0001a0d8 @ 0001a0d8

void FUN_0001a0d8(undefined4 param_1,undefined4 param_2,undefined4 param_3)

{
  float fVar1;
  float fVar2;
  int iVar3;
  int iVar4;
  int iVar5;
  int iVar6;
  int *piVar7;
  int iVar8;
  int iVar9;
  bool bVar10;
  float fVar11;
  float fVar12;
  undefined4 local_60;
  float local_5c;
  float local_58;
  float local_54;
  float local_50;
  int local_4c;
  
  local_4c = **(int **)(DAT_0001a26c + 0x1a0f4);
  local_54 = *(float *)(DAT_0001a268 + 0x1a0f2);
  local_50 = *(float *)(DAT_0001a268 + 0x1a0f6);
  FUN_00017084(&local_54,0);
  if (*(char *)(DAT_0001a270 + 0x1a10e) == '\x01') {
    piVar7 = (int *)(DAT_0001a278 + 0x1a11e);
    iVar8 = *(int *)(DAT_0001a274 + 0x1a11c);
    iVar5 = *(int *)(iVar8 + 0x6c);
    iVar3 = (uint)*(byte *)(iVar8 + 0xad) + iVar5;
    iVar6 = iVar3;
    if (iVar3 < 0) {
      iVar6 = iVar3 + 0xd;
    }
    iVar9 = iVar3 + -0xd;
    if (iVar3 + -0xd == 0 || iVar3 < 0xd) {
      iVar9 = iVar6;
    }
    if (*piVar7 == 0) {
      iVar6 = 0;
      iVar3 = DAT_0001a280 + 0x1a14a;
      do {
        FUN_0001a290();
        iVar4 = FUN_000127ac(iVar3,0);
        piVar7[iVar6] = iVar4;
        iVar6 = iVar6 + 1;
      } while (iVar6 != 0xd);
    }
    if (piVar7[iVar9 + -1] != 0) {
      local_5c = *(float *)(DAT_0001a284 + 0x1a17a);
      local_58 = *(float *)(DAT_0001a284 + 0x1a17e);
      FUN_00017084(&local_5c,1);
      fVar2 = local_50;
      fVar1 = local_54;
      fVar12 = local_5c * DAT_0001a260;
      local_60 = 0xffb4b4b4;
      fVar11 = local_58 * DAT_0001a264;
      bVar10 = iVar5 == 0;
      if (bVar10) {
        iVar5 = *(int *)(iVar8 + 0x70);
      }
      if (!bVar10 || iVar5 != 0) {
        local_60 = 0xff646464;
      }
      local_5c = fVar12;
      local_58 = fVar11;
      FUN_00012840(0xb,2);
      FUN_00012840(0xc,1);
      FUN_00012840(10,7);
      FUN_00012774(piVar7[iVar9 + -1],fVar1 - fVar12 * 0.5,fVar2 - fVar11 * 0.5,local_5c,local_58,
                   &local_60);
      FUN_00012840(10,0);
      FUN_00012840(0xb,0);
      FUN_00012840(0xc,0);
      goto LAB_0001a23c;
    }
  }
  (**(code **)(DAT_0001a288 + 0x1a238))(local_54,local_50,param_3);
LAB_0001a23c:
  if (**(int **)(DAT_0001a28c + 0x1a244) != local_4c) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_0001a290 @ 0001a290

void FUN_0001a290(void)

{
  int iVar1;
  undefined4 in_r3;
  undefined4 local_4;
  
  iVar1 = **(int **)(DAT_0001a2e4 + 0x1a2aa);
  local_4 = in_r3;
  __vsprintf_chk(DAT_0001a2dc + 0x1a2a4,0,0xff,DAT_0001a2e0 + 0x1a2a8,&local_4,&local_4);
  if (**(int **)(DAT_0001a2e8 + 0x1a2c6) != iVar1) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_0001a2ec @ 0001a2ec

void FUN_0001a2ec(void)

{
  undefined4 local_14;
  undefined4 local_10;
  int local_c;
  
  local_c = **(int **)(DAT_0001a33c + 0x1a2fc);
  local_14 = *(undefined4 *)(DAT_0001a338 + 0x1a2fa);
  local_10 = *(undefined4 *)(DAT_0001a338 + 0x1a2fe);
  FUN_00017084(&local_14,1);
  FUN_00018400(0,local_14);
  FUN_00018410(0,local_10);
  if (**(int **)(DAT_0001a340 + 0x1a328) != local_c) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_0001a344 @ 0001a344

void FUN_0001a344(void)

{
  float fVar1;
  undefined1 uVar2;
  float fVar3;
  int iVar4;
  int iVar5;
  undefined4 *puVar6;
  undefined4 uVar7;
  undefined4 *puVar8;
  int iVar9;
  undefined4 uVar10;
  int *piVar11;
  
  puVar6 = (undefined4 *)(DAT_0001a530 + 0x1a35a);
  iVar9 = DAT_0001a538 + 0x1a362;
  uVar2 = FUN_00013070(*puVar6,DAT_0001a534 + 0x1a35e,0);
  uVar7 = *puVar6;
  iVar4 = DAT_0001a540 + 0x1a378;
  *(undefined1 *)(DAT_0001a53c + 0x1a376) = uVar2;
  uVar7 = FUN_00013168(uVar7,iVar4,0xff000000,iVar9);
  piVar11 = (int *)(DAT_0001a544 + 0x1a390);
  puVar8 = (undefined4 *)(DAT_0001a548 + 0x1a3a0);
  (**(code **)(*(int *)*puVar8 + 0x20))((int *)*puVar8,*piVar11 + 0x3a347c,1);
  *(char *)(*piVar11 + 0x3a347c) = (char)((uint)uVar7 >> 0x18);
  (**(code **)(*(int *)*puVar8 + 0x20))((int *)*puVar8,*piVar11 + 0x3a347e,1);
  *(char *)(*piVar11 + 0x3a347e) = (char)uVar7;
  (**(code **)(*(int *)*puVar8 + 0x20))((int *)*puVar8,*piVar11 + 0x3a3484,1);
  *(char *)(*piVar11 + 0x3a3484) = (char)((uint)uVar7 >> 8);
  (**(code **)(*(int *)*puVar8 + 0x20))((int *)*puVar8,*piVar11 + 0x3a3486,1);
  iVar4 = DAT_0001a54c;
  *(char *)(*piVar11 + 0x3a3486) = (char)((uint)uVar7 >> 0x10);
  uVar2 = FUN_00013070(*puVar6,iVar4 + 0x1a404,1,iVar9);
  (**(code **)(*(int *)*puVar8 + 0x20))((int *)*puVar8,*piVar11 + 0x3a3464,1);
  *(undefined1 *)(*piVar11 + 0x3a3464) = uVar2;
  uVar2 = FUN_00012e04(*puVar6,DAT_0001a550 + 0x1a434,2,iVar9);
  (**(code **)(*(int *)*puVar8 + 0x20))((int *)*puVar8,*piVar11 + 0x3a3416,1);
  iVar4 = DAT_0001a554;
  *(undefined1 *)(*piVar11 + 0x3a3416) = uVar2;
  uVar2 = FUN_00012e04(*puVar6,iVar4 + 0x1a45a,1,iVar9);
  (**(code **)(*(int *)*puVar8 + 0x20))((int *)*puVar8,*piVar11 + 0x3a346a,1);
  iVar4 = DAT_0001a558;
  *(undefined1 *)(*piVar11 + 0x3a346a) = uVar2;
  uVar2 = FUN_00012e04(*puVar6,iVar4 + 0x1a484,1,iVar9);
  (**(code **)(*(int *)*puVar8 + 0x20))((int *)*puVar8,*piVar11 + 0x3a3476,1);
  *(undefined1 *)(*piVar11 + 0x3a3476) = uVar2;
  fVar3 = (float)FUN_00012f28(*puVar6,DAT_0001a55c + 0x1a4b4,0x40000000,iVar9);
  iVar4 = DAT_0001a560;
  fVar1 = DAT_0001a52c;
  uVar7 = *puVar6;
  iVar5 = DAT_0001a564 + 0x1a4d8;
  *(float *)(DAT_0001a560 + 0x1a4d0) = fVar3 / DAT_0001a52c;
  fVar3 = (float)FUN_00012f28(uVar7,iVar5,0x42be0000,iVar9);
  uVar7 = *puVar6;
  iVar5 = DAT_0001a568 + 0x1a4f8;
  *(float *)(iVar4 + 0x1a4d4) = fVar3 / fVar1;
  uVar7 = FUN_00012f28(uVar7,iVar5,0x3e99999a,iVar9);
  iVar4 = DAT_0001a56c;
  uVar10 = *puVar6;
  iVar5 = DAT_0001a570 + 0x1a516;
  *(undefined4 *)(DAT_0001a56c + 0x1a514) = uVar7;
  uVar7 = FUN_00012f28(uVar10,iVar5,0x3f4ccccd,iVar9);
  *(undefined4 *)(iVar4 + 0x1a518) = uVar7;
  return;
}


// ==== FUN_0001a574 @ 0001a574

void FUN_0001a574(void)

{
  int *piVar1;
  int *piVar2;
  undefined4 *puVar3;
  
  puVar3 = (undefined4 *)(DAT_0001a5c8 + 0x1a58c);
  piVar2 = (int *)(DAT_0001a5cc + 0x1a58e);
  piVar1 = (int *)*puVar3;
  (**(code **)(*piVar1 + 0xc0))
            (piVar1,*piVar2 + 0x3a352b,DAT_0001a5d0 + 0x1a598,DAT_0001a5d4 + 0x1a5a6);
  piVar1 = (int *)*puVar3;
                    /* WARNING: Could not recover jumptable at 0x0001a5c4. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (**(code **)(*piVar1 + 0xc0))
            (piVar1,*piVar2 + 0x3a3461,DAT_0001a5d8 + 0x1a5b8,DAT_0001a5dc + 0x1a5ba);
  return;
}


// ==== FUN_0001a5e0 @ 0001a5e0

void FUN_0001a5e0(float param_1,float param_2,undefined4 param_3)

{
  uint in_fpscr;
  float fVar1;
  float fVar2;
  float local_24;
  float local_20;
  int local_1c;
  
  local_1c = **(int **)(DAT_0001a678 + 0x1a5fa);
  local_24 = *(float *)(DAT_0001a674 + 0x1a5f6);
  local_20 = *(float *)(DAT_0001a674 + 0x1a5fa);
  FUN_00017084(&local_24,0);
  fVar1 = (float)VectorSignedToFloat(*(undefined4 *)(*(int *)(DAT_0001a67c + 0x1a61c) + 4),
                                     (byte)(in_fpscr >> 0x16) & 3);
  fVar2 = (float)VectorSignedToFloat(*(undefined4 *)(*(int *)(DAT_0001a67c + 0x1a61c) + 8),
                                     (byte)(in_fpscr >> 0x16) & 3);
  (**(code **)(DAT_0001a680 + 0x1a646))
            (param_1 + fVar1 * -0.5 + local_24,(param_2 - fVar2) + local_20,param_3);
  if (**(int **)(DAT_0001a684 + 0x1a662) != local_1c) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_0001a688 @ 0001a688

void FUN_0001a688(void)

{
  int *piVar1;
  float *pfVar2;
  float fVar3;
  float local_1c;
  float local_18;
  int local_14;
  
  local_14 = **(int **)(DAT_0001a710 + 0x1a69c);
  local_1c = *(float *)(DAT_0001a70c + 0x1a69a);
  local_18 = *(float *)(DAT_0001a70c + 0x1a69e);
  FUN_00017084(&local_1c,1);
  piVar1 = (int *)FUN_000126ac(DAT_0001a714 + 0x1a6b6);
  pfVar2 = (float *)&DAT_0001a704;
  if (*piVar1 == 1) {
    pfVar2 = (float *)&DAT_0001a708;
  }
  fVar3 = *pfVar2;
  FUN_00018400(0,local_1c * fVar3);
  FUN_00018410(0,fVar3 * local_18);
  if (**(int **)(DAT_0001a718 + 0x1a6f2) != local_14) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_0001a71c @ 0001a71c

void FUN_0001a71c(void)

{
  float fVar1;
  float local_1c;
  float local_18;
  int local_14;
  
  local_14 = **(int **)(DAT_0001a790 + 0x1a730);
  local_1c = *(float *)(DAT_0001a78c + 0x1a72e);
  local_18 = *(float *)(DAT_0001a78c + 0x1a732);
  FUN_00017084(&local_1c,1);
  fVar1 = DAT_0001a788;
  FUN_00018400(0,local_1c * DAT_0001a788);
  FUN_00018410(0,local_18 * fVar1);
  if (**(int **)(DAT_0001a794 + 0x1a774) != local_14) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_0001a798 @ 0001a798

void FUN_0001a798(void)

{
  undefined4 local_14;
  undefined4 local_10;
  int local_c;
  
  local_c = **(int **)(DAT_0001a7e8 + 0x1a7a8);
  local_14 = *(undefined4 *)(DAT_0001a7e4 + 0x1a7a6);
  local_10 = *(undefined4 *)(DAT_0001a7e4 + 0x1a7aa);
  FUN_00017084(&local_14,1);
  FUN_00018400(0,local_14);
  FUN_00018410(0,local_10);
  if (**(int **)(DAT_0001a7ec + 0x1a7d4) != local_c) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_0001a7f0 @ 0001a7f0

void FUN_0001a7f0(void)

{
  float fVar1;
  int iVar2;
  undefined1 uVar3;
  float fVar4;
  undefined4 uVar5;
  int iVar6;
  undefined4 *puVar7;
  undefined4 uVar8;
  int iVar9;
  int *piVar10;
  undefined4 *puVar11;
  
  puVar11 = (undefined4 *)(DAT_0001a968 + 0x1a808);
  iVar9 = DAT_0001a970 + 0x1a810;
  uVar3 = FUN_00012e04(*puVar11,DAT_0001a96c + 0x1a80c,1);
  piVar10 = (int *)(DAT_0001a974 + 0x1a820);
  puVar7 = (undefined4 *)(DAT_0001a978 + 0x1a830);
  (**(code **)(*(int *)*puVar7 + 0x20))((int *)*puVar7,*piVar10 + 0x438dd6,1);
  iVar2 = DAT_0001a97c;
  *(undefined1 *)(*piVar10 + 0x438dd6) = uVar3;
  uVar3 = FUN_00013070(*puVar11,iVar2 + 0x1a84a,1,iVar9);
  (**(code **)(*(int *)*puVar7 + 0x20))((int *)*puVar7,*piVar10 + 0x438dca,1);
  *(undefined1 *)(*piVar10 + 0x438dca) = uVar3;
  uVar3 = FUN_00012e04(*puVar11,DAT_0001a980 + 0x1a876,0,iVar9);
  (**(code **)(*(int *)*puVar7 + 0x20))((int *)*puVar7,*piVar10 + 0x438dc2,1);
  iVar2 = DAT_0001a984;
  *(undefined1 *)(*piVar10 + 0x438dc2) = uVar3;
  uVar3 = FUN_00012e04(*puVar11,iVar2 + 0x1a89c,1,iVar9);
  (**(code **)(*(int *)*puVar7 + 0x20))((int *)*puVar7,*piVar10 + 0x438e08,1);
  *(undefined1 *)(*piVar10 + 0x438e08) = uVar3;
  uVar3 = FUN_00012e04(*puVar11,DAT_0001a988 + 0x1a8c8,2,iVar9);
  (**(code **)(*(int *)*puVar7 + 0x20))((int *)*puVar7,*piVar10 + 0x438df0,1);
  *(undefined1 *)(*piVar10 + 0x438df0) = uVar3;
  fVar4 = (float)FUN_00012f28(*puVar11,DAT_0001a98c + 0x1a8f6,0x42480000,iVar9);
  iVar2 = DAT_0001a990;
  fVar1 = DAT_0001a964;
  uVar5 = *puVar11;
  iVar6 = DAT_0001a994 + 0x1a918;
  *(float *)(DAT_0001a990 + 0x1a910) = fVar4 / DAT_0001a964;
  fVar4 = (float)FUN_00012f28(uVar5,iVar6,0x42c20000,iVar9);
  uVar5 = *puVar11;
  iVar6 = DAT_0001a998 + 0x1a936;
  *(float *)(iVar2 + 0x1a914) = fVar4 / fVar1;
  uVar5 = FUN_00012f28(uVar5,iVar6,0x3ecccccd,iVar9);
  iVar2 = DAT_0001a99c;
  uVar8 = *puVar11;
  iVar6 = DAT_0001a9a0 + 0x1a94c;
  *(undefined4 *)(DAT_0001a99c + 0x1a94a) = uVar5;
  uVar5 = FUN_00012f28(uVar8,iVar6,0x3f800000,iVar9);
  *(undefined4 *)(iVar2 + 0x1a94e) = uVar5;
  return;
}


// ==== FUN_0001a9a4 @ 0001a9a4

void FUN_0001a9a4(void)

{
  undefined4 *puVar1;
  int *piVar2;
  
  puVar1 = (undefined4 *)(DAT_0001aa2c + 0x1a9ba);
  piVar2 = (int *)(DAT_0001aa30 + 0x1a9c0);
  (**(code **)(*(int *)*puVar1 + 0xc0))
            ((int *)*puVar1,*piVar2 + 0x438ea1,DAT_0001aa34 + 0x1a9cc,DAT_0001aa38 + 0x1a9d4);
  (**(code **)(*(int *)*puVar1 + 0xc0))
            ((int *)*puVar1,*piVar2 + 0x438f8f,DAT_0001aa3c + 0x1a9ec,DAT_0001aa40 + 0x1a9ee);
  (**(code **)(*(int *)*puVar1 + 0xc0))
            ((int *)*puVar1,*piVar2 + 0x439165,DAT_0001aa44 + 0x1aa06,DAT_0001aa48 + 0x1aa08);
                    /* WARNING: Could not recover jumptable at 0x0001aa28. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (**(code **)(*(int *)*puVar1 + 0xc0))
            ((int *)*puVar1,*piVar2 + 0x4391e9,DAT_0001aa4c + 0x1aa1c,DAT_0001aa50 + 0x1aa22);
  return;
}


// ==== FUN_0001aa54 @ 0001aa54

void FUN_0001aa54(undefined4 param_1,undefined4 param_2,undefined4 param_3)

{
  undefined4 local_1c;
  undefined4 uStack_18;
  int local_14;
  
  local_14 = **(int **)(DAT_0001aaa4 + 0x1aa66);
  local_1c = *(undefined4 *)(DAT_0001aaa0 + 0x1aa64);
  uStack_18 = *(undefined4 *)(DAT_0001aaa0 + 0x1aa68);
  FUN_00017084(&local_1c,0);
  (**(code **)(DAT_0001aaa8 + 0x1aa84))(local_1c,uStack_18,param_3);
  if (**(int **)(DAT_0001aaac + 0x1aa90) != local_14) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_0001aab0 @ 0001aab0

void FUN_0001aab0(void)

{
  float fVar1;
  int iVar2;
  undefined1 uVar3;
  undefined4 uVar4;
  float fVar5;
  int iVar6;
  undefined4 *puVar7;
  undefined4 *puVar8;
  int iVar9;
  int *piVar10;
  
  puVar7 = (undefined4 *)(DAT_0001ac34 + 0x1aac6);
  iVar9 = DAT_0001ac3c + 0x1aace;
  uVar4 = FUN_00013168(*puVar7,DAT_0001ac38 + 0x1aaca,0);
  piVar10 = (int *)(DAT_0001ac40 + 0x1aae2);
  puVar8 = (undefined4 *)(DAT_0001ac44 + 0x1aaf2);
  (**(code **)(*(int *)*puVar8 + 0x20))((int *)*puVar8,*piVar10 + 0x4388f0,1);
  *(char *)(*piVar10 + 0x4388f0) = (char)uVar4;
  (**(code **)(*(int *)*puVar8 + 0x20))((int *)*puVar8,*piVar10 + 0x4388f2,1);
  *(char *)(*piVar10 + 0x4388f2) = (char)((uint)uVar4 >> 8);
  (**(code **)(*(int *)*puVar8 + 0x20))((int *)*puVar8,*piVar10 + 0x4388f4,1);
  iVar2 = DAT_0001ac48;
  *(char *)(*piVar10 + 0x4388f4) = (char)((uint)uVar4 >> 0x10);
  uVar3 = FUN_00013070(*puVar7,iVar2 + 0x1ab3c,1,iVar9);
  (**(code **)(*(int *)*puVar8 + 0x20))((int *)*puVar8,*piVar10 + 0x438880,1);
  *(undefined1 *)(*piVar10 + 0x438880) = uVar3;
  uVar3 = FUN_00012e04(*puVar7,DAT_0001ac4c + 0x1ab6c,2,iVar9);
  (**(code **)(*(int *)*puVar8 + 0x20))((int *)*puVar8,*piVar10 + 0x4388c2,1);
  *(undefined1 *)(*piVar10 + 0x4388c2) = uVar3;
  uVar3 = FUN_00012e04(*puVar7,DAT_0001ac50 + 0x1ab96,2,iVar9);
  (**(code **)(*(int *)*puVar8 + 0x20))((int *)*puVar8,*piVar10 + 0x4388b6,1);
  iVar2 = DAT_0001ac54;
  *(undefined1 *)(*piVar10 + 0x4388b6) = uVar3;
  uVar3 = FUN_00012e04(*puVar7,iVar2 + 0x1abbc,1,iVar9);
  (**(code **)(*(int *)*puVar8 + 0x20))((int *)*puVar8,*piVar10 + 0x4388c8,1);
  *(undefined1 *)(*piVar10 + 0x4388c8) = uVar3;
  fVar5 = (float)FUN_00012f28(*puVar7,DAT_0001ac58 + 0x1abee,0x42c40000,iVar9);
  iVar2 = DAT_0001ac5c;
  fVar1 = DAT_0001ac30;
  uVar4 = *puVar7;
  iVar6 = DAT_0001ac60 + 0x1ac12;
  *(float *)(DAT_0001ac5c + 0x1ac0a) = fVar5 / DAT_0001ac30;
  fVar5 = (float)FUN_00012f28(uVar4,iVar6,0x42be0000,iVar9);
  *(float *)(iVar2 + 0x1ac0e) = fVar5 / fVar1;
  return;
}


// ==== FUN_0001ac64 @ 0001ac64

void FUN_0001ac64(void)

{
                    /* WARNING: Could not recover jumptable at 0x0001ac92. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (**(code **)(**(int **)(DAT_0001ac94 + 0x1ac78) + 0xc0))
            (*(int **)(DAT_0001ac94 + 0x1ac78),*(int *)(DAT_0001ac98 + 0x1ac7a) + 0x438983,
             DAT_0001ac9c + 0x1ac84,DAT_0001aca0 + 0x1ac88);
  return;
}


// ==== FUN_0001aca4 @ 0001aca4

void FUN_0001aca4(void)

{
  float fVar1;
  undefined1 uVar2;
  undefined2 uVar3;
  undefined2 *puVar4;
  float fVar5;
  int iVar6;
  undefined4 uVar7;
  int iVar8;
  undefined4 uVar9;
  int iVar10;
  undefined4 *puVar11;
  
  puVar11 = (undefined4 *)(DAT_0001ae24 + 0x1acbc);
  iVar10 = DAT_0001ae2c + 0x1acc0;
  uVar2 = FUN_00013070(*puVar11,DAT_0001ae28 + 0x1acbe,0,iVar10);
  iVar6 = DAT_0001ae34;
  uVar9 = *puVar11;
  *(undefined1 *)(DAT_0001ae30 + 0x1acd0) = uVar2;
  uVar9 = FUN_00013168(uVar9,iVar6 + 0x1acd8,0xff106290,iVar10);
  uVar7 = *puVar11;
  iVar6 = DAT_0001ae3c + 0x1acee;
  *(undefined4 *)(DAT_0001ae38 + 0x1acec) = uVar9;
  uVar9 = FUN_00013168(uVar7,iVar6,0xff000000,iVar10);
  uVar7 = *puVar11;
  iVar6 = DAT_0001ae44 + 0x1ad08;
  iVar8 = DAT_0001ae48 + 0x1ad0a;
  *(undefined4 *)(DAT_0001ae40 + 0x1ad04) = uVar9;
  puVar4 = (undefined2 *)FUN_00012d98(uVar7,iVar6,iVar8,iVar10);
  iVar6 = DAT_0001ae50;
  uVar9 = *puVar11;
  *(undefined2 *)(DAT_0001ae4c + 0x1ad1c) = *puVar4;
  uVar2 = FUN_00012e04(uVar9,iVar6 + 0x1ad24,4,iVar10);
  uVar9 = *puVar11;
  iVar6 = DAT_0001ae58 + 0x1ad34;
  *(undefined1 *)(DAT_0001ae54 + 0x1ad32) = uVar2;
  uVar3 = FUN_00012e04(uVar9,iVar6,1,iVar10);
  uVar9 = *puVar11;
  iVar6 = DAT_0001ae60 + 0x1ad4a;
  *(undefined2 *)(DAT_0001ae5c + 0x1ad48) = uVar3;
  uVar2 = FUN_00012e04(uVar9,iVar6,2,iVar10);
  uVar9 = *puVar11;
  iVar6 = DAT_0001ae68 + 0x1ad60;
  *(undefined1 *)(DAT_0001ae64 + 0x1ad5e) = uVar2;
  uVar2 = FUN_00013070(uVar9,iVar6,1,iVar10);
  iVar6 = DAT_0001ae70;
  uVar9 = *puVar11;
  *(undefined1 *)(DAT_0001ae6c + 0x1ad74) = uVar2;
  uVar9 = FUN_00012f28(uVar9,iVar6 + 0x1ad7c,0x3e19999a,iVar10);
  iVar6 = DAT_0001ae78;
  uVar7 = *puVar11;
  *(undefined4 *)(DAT_0001ae74 + 0x1ad90) = uVar9;
  fVar5 = (float)FUN_00012f28(uVar7,iVar6 + 0x1ad96,0x42c00000,iVar10);
  iVar6 = DAT_0001ae7c;
  fVar1 = DAT_0001ae20;
  uVar9 = *puVar11;
  iVar8 = DAT_0001ae80 + 0x1adc2;
  *(float *)(DAT_0001ae7c + 0x1adba) = fVar5 / DAT_0001ae20;
  fVar5 = (float)FUN_00012f28(uVar9,iVar8,0x41cf3333,iVar10);
  uVar9 = *puVar11;
  iVar8 = DAT_0001ae84 + 0x1adde;
  *(float *)(iVar6 + 0x1adbe) = fVar5 / fVar1;
  uVar9 = FUN_00012f28(uVar9,iVar8,0x3f000000,iVar10);
  iVar6 = DAT_0001ae88;
  uVar7 = *puVar11;
  iVar8 = DAT_0001ae8c + 0x1adf2;
  *(undefined4 *)(DAT_0001ae88 + 0x1adf0) = uVar9;
  uVar9 = FUN_00012f28(uVar7,iVar8,0x3fc00000,iVar10);
  uVar7 = *puVar11;
  iVar8 = DAT_0001ae90 + 0x1ae06;
  *(undefined4 *)(iVar6 + 0x1adf4) = uVar9;
  uVar2 = FUN_00013070(uVar7,iVar8,0,iVar10);
  *(undefined1 *)(DAT_0001ae94 + 0x1ae14) = uVar2;
  return;
}


// ==== FUN_0001aec8 @ 0001aec8

void FUN_0001aec8(int param_1)

{
  undefined1 uVar1;
  undefined1 uVar2;
  undefined1 uVar3;
  undefined1 uVar4;
  undefined1 uVar5;
  undefined1 uVar6;
  byte bVar7;
  float fVar8;
  int iVar9;
  uint uVar10;
  uint uVar11;
  uint uVar12;
  uint uVar13;
  undefined1 *puVar14;
  byte bVar15;
  int iVar16;
  uint uVar17;
  uint uVar18;
  byte bVar19;
  bool bVar20;
  bool bVar21;
  uint in_fpscr;
  float fVar22;
  float fVar23;
  float fVar24;
  float fVar25;
  undefined1 local_a0;
  undefined1 local_9f;
  undefined1 local_9e;
  byte local_9d;
  undefined1 local_9c;
  undefined1 local_9b;
  undefined1 local_9a;
  byte local_99;
  undefined1 local_98;
  undefined1 local_97;
  undefined1 local_96;
  byte local_95;
  undefined2 local_94;
  undefined1 local_92;
  undefined1 local_91;
  undefined4 local_90;
  undefined4 local_8c;
  float local_88;
  float local_84;
  undefined1 auStack_80 [20];
  undefined1 auStack_6c [40];
  int local_44;
  
  local_44 = **(int **)(DAT_0001b170 + 0x1aee0);
  iVar9 = FUN_00012824(0xffffffff);
  uVar17 = *(uint *)(iVar9 + 0x2c);
  uVar10 = *(uint *)(iVar9 + 0x30);
  uVar11 = *(uint *)(param_1 + 0x90);
  if (uVar11 != uVar17) {
    *(uint *)(param_1 + 0x90) = uVar17;
  }
  if ((int)uVar17 < 1 || uVar11 != uVar17) {
    uVar12 = (uint)*(byte *)(DAT_0001b174 + 0x1af1e) << 0x1f;
    uVar11 = uVar12;
    if (uVar12 == 0) {
      uVar11 = uVar10;
    }
    if (uVar12 == 0 && uVar11 == 0) goto LAB_0001b14a;
  }
  FUN_0001b30c(0,0);
  FUN_0001b348(*(undefined1 *)(DAT_0001b178 + 0x1af3c));
  FUN_0001b2f0(*(undefined1 *)(DAT_0001b17c + 0x1af46));
  FUN_0001b21c(*(undefined1 *)(DAT_0001b180 + 0x1af50));
  __strcpy_chk(auStack_80,DAT_0001b184 + 0x1af5e,0x14);
  FUN_00012880(auStack_80,auStack_6c);
  local_88 = *(float *)(DAT_0001b188 + 0x1af70);
  local_84 = *(float *)(DAT_0001b188 + 0x1af74);
  FUN_00017084(&local_88,0);
  local_90 = *(undefined4 *)(DAT_0001b18c + 0x1af84);
  local_8c = *(undefined4 *)(DAT_0001b18c + 0x1af88);
  FUN_00017084(&local_90,1);
  fVar8 = DAT_0001b16c;
  if (*(char *)(DAT_0001b190 + 0x1b011) == '\x01') {
    local_84 = **(float **)(DAT_0001b194 + 0x1afa8) + local_84;
  }
  uVar11 = **(uint **)(DAT_0001b198 + 0x1afbe);
  uVar1 = *(undefined1 *)(DAT_0001b19c + 0x1afd4);
  uVar2 = *(undefined1 *)(DAT_0001b19c + 0x1afd5);
  uVar3 = *(undefined1 *)(DAT_0001b19c + 0x1afd6);
  uVar18 = uVar11 % 0x15e;
  uVar4 = *(undefined1 *)(DAT_0001b1a0 + 0x1afd8);
  uVar5 = *(undefined1 *)(DAT_0001b1a0 + 0x1afd9);
  uVar12 = 0;
  uVar6 = *(undefined1 *)(DAT_0001b1a0 + 0x1afda);
  bVar7 = *(byte *)(param_1 + 0x4c);
  bVar15 = *(byte *)(DAT_0001b1a0 + 0x1afdb);
  if (bVar7 < *(byte *)(DAT_0001b1a0 + 0x1afdb)) {
    bVar15 = bVar7;
  }
  bVar19 = *(byte *)(DAT_0001b19c + 0x1afd7);
  if (bVar7 < *(byte *)(DAT_0001b19c + 0x1afd7)) {
    bVar19 = bVar7;
  }
  iVar16 = *(int *)(DAT_0001b1a4 + 0x1b01c);
  fVar24 = local_84;
  fVar25 = local_88;
  do {
    FUN_0001b2d4((int)*(short *)(DAT_0001b1a8 + 0x1b0b6));
    local_91 = *(undefined1 *)(param_1 + 0x4c);
    local_92 = 0;
    local_94 = 0;
    FUN_0001b270(&local_94);
    FUN_00018400(0,local_90);
    FUN_00018410(0,local_8c);
    if (uVar12 < uVar17) {
      bVar21 = 0x95 < uVar18;
      bVar20 = uVar18 == 0x96;
      if (uVar18 < 0x97) {
        uVar13 = *(int *)(iVar9 + 0xc) + 2000;
        bVar21 = uVar13 <= uVar11;
        bVar20 = uVar11 == uVar13;
      }
      if (bVar21 && !bVar20) {
        puVar14 = &local_a0;
        local_a0 = uVar1;
        local_9f = uVar2;
        local_9e = uVar3;
        local_9d = bVar19;
LAB_0001b13e:
        FUN_0001b1d4(puVar14);
        goto LAB_0001b052;
      }
    }
    else {
      if ((uVar12 < uVar10) && (0x96 < uVar18)) {
        puVar14 = &local_98;
        local_98 = uVar1;
        local_97 = uVar2;
        local_96 = uVar3;
        local_95 = bVar19;
        goto LAB_0001b13e;
      }
      FUN_0001b2d4(0);
      local_9c = uVar4;
      local_9b = uVar5;
      local_9a = uVar6;
      local_99 = bVar15;
      FUN_0001b1d4(&local_9c);
      FUN_00018400(0,local_90);
      FUN_00018410(0,local_8c);
LAB_0001b052:
      fVar23 = local_84;
      fVar22 = fVar25;
      if (*(char *)(DAT_0001b1b4 + 0x1b062) != '\0') {
        fVar23 = fVar24;
        fVar22 = local_88;
      }
      FUN_0001b3a8(fVar22,fVar23,auStack_6c);
      FUN_0001b364();
    }
    uVar12 = uVar12 + 1;
    fVar22 = (float)VectorSignedToFloat(*(undefined4 *)(iVar16 + 4),(byte)(in_fpscr >> 0x16) & 3);
    fVar23 = (float)VectorSignedToFloat(*(undefined4 *)(iVar16 + 8),(byte)(in_fpscr >> 0x16) & 3);
    fVar25 = fVar25 + fVar22 * fVar8 * *(float *)(DAT_0001b1ac + 0x1b09c);
    fVar24 = fVar24 + fVar23 * fVar8 * *(float *)(DAT_0001b1ac + 0x1b09c);
  } while (uVar12 != 6);
  FUN_0001b2d4(0);
LAB_0001b14a:
  if (**(int **)(DAT_0001b1b0 + 0x1b152) != local_44) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_0001b1b8 @ 0001b1b8

void FUN_0001b1b8(undefined4 param_1)

{
  code *UNRECOVERED_JUMPTABLE;
  
  UNRECOVERED_JUMPTABLE = (code *)FUN_000126ac(DAT_0001b1d0 + 0x1b1c4);
                    /* WARNING: Could not recover jumptable at 0x0001b1ce. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*UNRECOVERED_JUMPTABLE)(param_1);
  return;
}


// ==== FUN_0001b1d4 @ 0001b1d4

void FUN_0001b1d4(undefined4 *param_1)

{
  code *pcVar1;
  undefined4 local_18;
  int local_14;
  
  local_14 = **(int **)(DAT_0001b214 + 0x1b1e6);
  pcVar1 = (code *)FUN_000126ac(DAT_0001b210 + 0x1b1e4);
  local_18 = *param_1;
  (*pcVar1)(&local_18);
  if (**(int **)(DAT_0001b218 + 0x1b200) != local_14) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_0001b21c @ 0001b21c

void FUN_0001b21c(undefined4 param_1)

{
  code *UNRECOVERED_JUMPTABLE;
  
  UNRECOVERED_JUMPTABLE = (code *)FUN_000126ac(DAT_0001b234 + 0x1b228);
                    /* WARNING: Could not recover jumptable at 0x0001b232. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*UNRECOVERED_JUMPTABLE)(param_1);
  return;
}


// ==== FUN_0001b238 @ 0001b238

void FUN_0001b238(undefined4 param_1)

{
  code *UNRECOVERED_JUMPTABLE;
  
  UNRECOVERED_JUMPTABLE = (code *)FUN_000126ac(DAT_0001b250 + 0x1b244);
                    /* WARNING: Could not recover jumptable at 0x0001b24e. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*UNRECOVERED_JUMPTABLE)(param_1);
  return;
}


// ==== FUN_0001b254 @ 0001b254

void FUN_0001b254(undefined4 param_1)

{
  code *UNRECOVERED_JUMPTABLE;
  
  UNRECOVERED_JUMPTABLE = (code *)FUN_000126ac(DAT_0001b26c + 0x1b260);
                    /* WARNING: Could not recover jumptable at 0x0001b26a. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*UNRECOVERED_JUMPTABLE)(param_1);
  return;
}


// ==== FUN_0001b270 @ 0001b270

void FUN_0001b270(undefined4 *param_1)

{
  code *pcVar1;
  undefined4 local_18;
  int local_14;
  
  local_14 = **(int **)(DAT_0001b2b0 + 0x1b282);
  pcVar1 = (code *)FUN_000126ac(DAT_0001b2ac + 0x1b280);
  local_18 = *param_1;
  (*pcVar1)(&local_18);
  if (**(int **)(DAT_0001b2b4 + 0x1b29c) != local_14) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ==== FUN_0001b2b8 @ 0001b2b8

void FUN_0001b2b8(undefined4 param_1)

{
  code *UNRECOVERED_JUMPTABLE;
  
  UNRECOVERED_JUMPTABLE = (code *)FUN_000126ac(DAT_0001b2d0 + 0x1b2c4);
                    /* WARNING: Could not recover jumptable at 0x0001b2ce. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*UNRECOVERED_JUMPTABLE)(param_1);
  return;
}


// ==== FUN_0001b2d4 @ 0001b2d4

void FUN_0001b2d4(undefined4 param_1)

{
  code *UNRECOVERED_JUMPTABLE;
  
  UNRECOVERED_JUMPTABLE = (code *)FUN_000126ac(DAT_0001b2ec + 0x1b2e0);
                    /* WARNING: Could not recover jumptable at 0x0001b2ea. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*UNRECOVERED_JUMPTABLE)(param_1);
  return;
}


// ==== FUN_0001b2f0 @ 0001b2f0

void FUN_0001b2f0(undefined4 param_1)

{
  code *UNRECOVERED_JUMPTABLE;
  
  UNRECOVERED_JUMPTABLE = (code *)FUN_000126ac(DAT_0001b308 + 0x1b2fc);
                    /* WARNING: Could not recover jumptable at 0x0001b306. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*UNRECOVERED_JUMPTABLE)(param_1);
  return;
}


// ==== FUN_0001b30c @ 0001b30c

void FUN_0001b30c(undefined4 param_1,undefined4 param_2)

{
  code *UNRECOVERED_JUMPTABLE;
  
  UNRECOVERED_JUMPTABLE = (code *)FUN_000126ac(DAT_0001b328 + 0x1b31a);
                    /* WARNING: Could not recover jumptable at 0x0001b326. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*UNRECOVERED_JUMPTABLE)(param_1,param_2);
  return;
}


// ==== FUN_0001b32c @ 0001b32c

void FUN_0001b32c(undefined4 param_1)

{
  code *UNRECOVERED_JUMPTABLE;
  
  UNRECOVERED_JUMPTABLE = (code *)FUN_000126ac(DAT_0001b344 + 0x1b338);
                    /* WARNING: Could not recover jumptable at 0x0001b342. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*UNRECOVERED_JUMPTABLE)(param_1);
  return;
}


// ==== FUN_0001b348 @ 0001b348

void FUN_0001b348(undefined4 param_1)

{
  code *UNRECOVERED_JUMPTABLE;
  
  UNRECOVERED_JUMPTABLE = (code *)FUN_000126ac(DAT_0001b360 + 0x1b354);
                    /* WARNING: Could not recover jumptable at 0x0001b35e. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*UNRECOVERED_JUMPTABLE)(param_1);
  return;
}


// ==== FUN_0001b364 @ 0001b364

void FUN_0001b364(void)

{
  code *UNRECOVERED_JUMPTABLE;
  
  UNRECOVERED_JUMPTABLE = (code *)FUN_000126ac(DAT_0001b378 + 0x1b36e);
                    /* WARNING: Could not recover jumptable at 0x0001b374. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*UNRECOVERED_JUMPTABLE)();
  return;
}


// ==== FUN_0001b37c @ 0001b37c

void FUN_0001b37c(undefined4 param_1,undefined4 param_2,undefined4 param_3)

{
  code *UNRECOVERED_JUMPTABLE;
  
  UNRECOVERED_JUMPTABLE = (code *)FUN_000126ac(DAT_0001b3a4 + 0x1b390);
                    /* WARNING: Could not recover jumptable at 0x0001b3a2. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*UNRECOVERED_JUMPTABLE)(param_1,param_2,param_3);
  return;
}


// ==== FUN_0001b3a8 @ 0001b3a8

void FUN_0001b3a8(undefined4 param_1,undefined4 param_2,undefined4 param_3)

{
  code *UNRECOVERED_JUMPTABLE;
  
  UNRECOVERED_JUMPTABLE = (code *)FUN_000126ac(DAT_0001b3d0 + 0x1b3bc);
                    /* WARNING: Could not recover jumptable at 0x0001b3ce. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*UNRECOVERED_JUMPTABLE)(param_1,param_2,param_3);
  return;
}


// ==== _INIT_7 @ 0001b3d4

void _INIT_7(void)

{
  undefined4 uVar1;
  int iVar2;
  
  uVar1 = FUN_000126ac(DAT_0001b438 + 0x1b3de);
  iVar2 = DAT_0001b440 + 0x1b3ea;
  *(undefined4 *)(DAT_0001b43c + 0x1b3e8) = uVar1;
  uVar1 = FUN_000126ac(iVar2);
  iVar2 = DAT_0001b448 + 0x1b3fa;
  *(undefined4 *)(DAT_0001b444 + 0x1b3f8) = uVar1;
  uVar1 = FUN_000126ac(iVar2);
  iVar2 = DAT_0001b450 + 0x1b40a;
  *(undefined4 *)(DAT_0001b44c + 0x1b408) = uVar1;
  uVar1 = FUN_000126ac(iVar2);
  iVar2 = DAT_0001b458 + 0x1b41a;
  *(undefined4 *)(DAT_0001b454 + 0x1b418) = uVar1;
  uVar1 = FUN_000126ac(iVar2);
  iVar2 = DAT_0001b460 + 0x1b42a;
  *(undefined4 *)(DAT_0001b45c + 0x1b428) = uVar1;
  uVar1 = FUN_000126ac(iVar2);
  *(undefined4 *)(DAT_0001b464 + 0x1b436) = uVar1;
  return;
}


// ==== FUN_0001b468 @ 0001b468

undefined8 FUN_0001b468(undefined4 param_1,undefined4 param_2)

{
  double dVar1;
  undefined8 uVar2;
  
  if ((double)CONCAT44(param_2,param_1) < 0.0) {
    dVar1 = -(double)CONCAT44(param_2,param_1);
    uVar2 = FUN_0001b498(SUB84(dVar1,0),(int)((ulonglong)dVar1 >> 0x20));
    return CONCAT44(-((int)((ulonglong)uVar2 >> 0x20) + (uint)((int)uVar2 != 0)),-(int)uVar2);
  }
  uVar2 = FUN_0001b498();
  return uVar2;
}


// ==== FUN_0001b498 @ 0001b498

undefined8 FUN_0001b498(undefined4 param_1,undefined4 param_2)

{
  uint uVar1;
  uint uVar2;
  int iVar3;
  uint in_fpscr;
  double dVar4;
  double dVar5;
  
  dVar4 = (double)CONCAT44(param_2,param_1);
  uVar1 = in_fpscr & 0xfffffff | (uint)(dVar4 == 0.0) << 0x1e;
  uVar2 = uVar1 | (uint)(0.0 <= dVar4) << 0x1d;
  if (!(bool)((byte)(uVar2 >> 0x1d) & 1) || SUB41(uVar1 >> 0x1e,0)) {
    return 0;
  }
  iVar3 = (uint)(0.0 < dVar4 * DAT_0001b4d8) * (int)(longlong)(dVar4 * DAT_0001b4d8);
  dVar5 = (double)VectorUnsignedToFloat(iVar3,(byte)(uVar2 >> 0x16) & 3);
  dVar4 = dVar4 + dVar5 * DAT_0001b4e0;
  return CONCAT44(iVar3,(uint)(0.0 < dVar4) * (int)(longlong)dVar4);
}


// ==== FUN_0001b4e8 @ 0001b4e8

double FUN_0001b4e8(uint param_1,undefined4 param_2)

{
  uint in_fpscr;
  double dVar1;
  
  dVar1 = (double)VectorSignedToFloat(param_2,(byte)(in_fpscr >> 0x16) & 3);
  return DAT_0001b520 + dVar1 * DAT_0001b518 + (double)((ulonglong)param_1 + 0x4330000000000000);
}


// ==== FUN_0001b528 @ 0001b528

void FUN_0001b528(undefined4 param_1,undefined4 param_2)

{
  undefined1 local_8 [4];
  
  FUN_0001b544(param_1,param_2,local_8);
  return;
}


// ==== FUN_0001b544 @ 0001b544

uint FUN_0001b544(uint param_1,uint param_2,uint *param_3)

{
  uint uVar1;
  
  if (param_2 == 0) {
    uVar1 = FUN_0001b724(0);
    return uVar1;
  }
  if (param_2 == 1) {
    *param_3 = 0;
    return param_1;
  }
  if (param_1 < param_2) {
    *param_3 = param_1;
    return 0;
  }
                    /* WARNING: Could not recover jumptable at 0x0001b574. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  uVar1 = (*(code *)(&UNK_0001b6ec + (LZCOUNT(param_2) - LZCOUNT(param_1)) * -0xc))();
  return uVar1;
}


// ==== FUN_0001b724 @ 0001b724

void FUN_0001b724(void)

{
  return;
}


// ==== FUN_0001b728 @ 0001b728

void FUN_0001b728(void)

{
  FUN_0001b74c();
  return;
}


// ==== FUN_0001b74c @ 0001b74c

ulonglong FUN_0001b74c(uint param_1,uint param_2,uint param_3,uint param_4,uint *param_5)

{
  byte bVar1;
  byte bVar2;
  byte bVar3;
  byte bVar4;
  uint uVar5;
  uint uVar6;
  uint uVar7;
  uint uVar8;
  uint uVar9;
  uint uVar10;
  uint uVar11;
  uint uVar12;
  uint uVar13;
  ulonglong uVar14;
  
  if (param_2 == 0) {
    if (param_4 == 0) {
      uVar14 = FUN_0001ba2c(param_1,param_3);
      uVar8 = (uint)uVar14;
      if (param_5 != (uint *)0x0) {
        uVar8 = param_1 - uVar8 * param_3;
        uVar14 = uVar14 & 0xffffffff;
      }
LAB_0001b9a0:
      if (param_5 != (uint *)0x0) {
        *param_5 = uVar8;
        param_5[1] = (uint)(uVar14 >> 0x20);
      }
      return uVar14 & 0xffffffff;
    }
    if (param_5 != (uint *)0x0) {
      param_2 = 0;
      *param_5 = param_1;
      param_5[1] = 0;
      param_1 = 0;
LAB_0001b7bc:
      return CONCAT44(param_2,param_1);
    }
  }
  else {
    if (param_3 != 0) {
      if (param_4 == 0) {
        if ((param_3 & param_3 - 1) == 0) {
          if (param_5 != (uint *)0x0) {
            *param_5 = param_3 - 1 & param_1;
            param_5[1] = 0;
          }
          if (param_3 != 1) {
            bVar2 = (byte)param_3;
            bVar3 = (byte)(param_3 >> 8);
            bVar4 = (byte)(param_3 >> 0x10);
            bVar1 = (byte)(param_3 >> 0x18);
            uVar8 = LZCOUNT((uint)(byte)((((((((bVar2 & 1) << 1 | bVar2 >> 1 & 1) << 1 |
                                             bVar2 >> 2 & 1) << 1 | bVar2 >> 3 & 1) << 1 |
                                           bVar2 >> 4 & 1) << 1 | bVar2 >> 5 & 1) << 1 |
                                         bVar2 >> 6 & 1) << 1 | bVar2 >> 7) << 0x18 |
                            (uint)(byte)((((((((bVar3 & 1) << 1 | bVar3 >> 1 & 1) << 1 |
                                             bVar3 >> 2 & 1) << 1 | bVar3 >> 3 & 1) << 1 |
                                           bVar3 >> 4 & 1) << 1 | bVar3 >> 5 & 1) << 1 |
                                         bVar3 >> 6 & 1) << 1 | bVar3 >> 7) << 0x10 |
                            (uint)(byte)((((((((bVar4 & 1) << 1 | bVar4 >> 1 & 1) << 1 |
                                             bVar4 >> 2 & 1) << 1 | bVar4 >> 3 & 1) << 1 |
                                           bVar4 >> 4 & 1) << 1 | bVar4 >> 5 & 1) << 1 |
                                         bVar4 >> 6 & 1) << 1 | bVar4 >> 7) << 8 |
                            (uint)(byte)((((((((bVar1 & 1) << 1 | bVar1 >> 1 & 1) << 1 |
                                             bVar1 >> 2 & 1) << 1 | bVar1 >> 3 & 1) << 1 |
                                           bVar1 >> 4 & 1) << 1 | bVar1 >> 5 & 1) << 1 |
                                         bVar1 >> 6 & 1) << 1 | bVar1 >> 7));
            return CONCAT44(param_2 >> uVar8,
                            (param_2 << 1) << (~uVar8 & 0x1f) | param_1 >> (uVar8 & 0x1f));
          }
          goto LAB_0001b7bc;
        }
        uVar8 = 0;
        uVar13 = (LZCOUNT(param_3) - LZCOUNT(param_2)) + 0x21;
        if (uVar13 != 0x20) {
          if (uVar13 < 0x20) {
            uVar12 = param_2 >> (uVar13 & 0xff);
            uVar11 = param_2 << (0x20 - uVar13 & 0xff) | param_1 >> (uVar13 & 0xff);
            uVar6 = param_1 << (0x20 - uVar13 & 0xff);
          }
          else {
            uVar8 = (LZCOUNT(param_3) - LZCOUNT(param_2)) + 1;
            uVar12 = 0;
            uVar6 = param_2 << (0x40 - uVar13 & 0xff) | param_1 >> (uVar8 & 0xff);
            uVar11 = param_2 >> (uVar8 & 0xff);
            uVar8 = param_1 << (0x40 - uVar13 & 0xff);
          }
          goto LAB_0001b904;
        }
      }
      else {
        uVar6 = LZCOUNT(param_4) - LZCOUNT(param_2);
        if (0x1f < uVar6) goto LAB_0001b78c;
        uVar13 = uVar6 + 1;
        uVar8 = 0;
        if (uVar13 != 0x20) {
          uVar12 = param_2 >> (uVar13 & 0xff);
          uVar11 = param_2 << (0x1f - uVar6 & 0xff) | param_1 >> (uVar13 & 0xff);
          uVar6 = param_1 << (0x1f - uVar6 & 0xff);
          goto LAB_0001b904;
        }
      }
      uVar8 = 0;
      uVar13 = 0x20;
      uVar12 = 0;
      uVar11 = param_2;
      uVar6 = param_1;
LAB_0001b904:
      uVar7 = 0;
      do {
        uVar9 = uVar8;
        uVar10 = uVar11 << 1 | uVar6 >> 0x1f;
        uVar8 = uVar12 << 1 | uVar11 >> 0x1f;
        uVar5 = param_4 + ~uVar8 + (uint)CARRY4(param_3,~uVar10);
        uVar11 = uVar10 - param_3;
        uVar12 = uVar8 - (param_4 + (uVar10 < param_3));
        if (-1 < (int)uVar5) {
          uVar11 = uVar10;
          uVar12 = uVar8;
        }
        uVar5 = uVar5 >> 0x1f;
        uVar6 = uVar6 << 1 | uVar9 >> 0x1f;
        uVar8 = uVar7 | uVar9 << 1;
        uVar13 = uVar13 - 1;
        uVar7 = uVar5;
      } while (uVar13 != 0);
      if (param_5 != (uint *)0x0) {
        *param_5 = uVar11;
        param_5[1] = uVar12;
      }
      return CONCAT44((uVar9 & 0x7fffffff) >> 0x1e | uVar6 << 1,uVar5 | uVar8 << 1);
    }
    if (param_4 == 0) {
      return CONCAT44(param_2,param_1);
    }
    if (param_1 == 0) {
      uVar14 = FUN_0001ba2c(param_2,param_4);
      uVar8 = (uint)uVar14;
      if (param_5 != (uint *)0x0) {
        uVar14 = CONCAT44(param_2 - uVar8 * param_4,uVar8);
        uVar8 = 0;
      }
      goto LAB_0001b9a0;
    }
    if ((param_4 & param_4 - 1) == 0) {
      bVar2 = (byte)param_4;
      bVar3 = (byte)(param_4 >> 8);
      bVar4 = (byte)(param_4 >> 0x10);
      bVar1 = (byte)(param_4 >> 0x18);
      if (param_5 != (uint *)0x0) {
        *param_5 = param_1;
        param_5[1] = param_4 - 1 & param_2;
      }
      param_2 = param_2 >>
                LZCOUNT((uint)(byte)((((((((bVar2 & 1) << 1 | bVar2 >> 1 & 1) << 1 | bVar2 >> 2 & 1)
                                         << 1 | bVar2 >> 3 & 1) << 1 | bVar2 >> 4 & 1) << 1 |
                                      bVar2 >> 5 & 1) << 1 | bVar2 >> 6 & 1) << 1 | bVar2 >> 7) <<
                        0x18 | (uint)(byte)((((((((bVar3 & 1) << 1 | bVar3 >> 1 & 1) << 1 |
                                                bVar3 >> 2 & 1) << 1 | bVar3 >> 3 & 1) << 1 |
                                              bVar3 >> 4 & 1) << 1 | bVar3 >> 5 & 1) << 1 |
                                            bVar3 >> 6 & 1) << 1 | bVar3 >> 7) << 0x10 |
                        (uint)(byte)((((((((bVar4 & 1) << 1 | bVar4 >> 1 & 1) << 1 | bVar4 >> 2 & 1)
                                         << 1 | bVar4 >> 3 & 1) << 1 | bVar4 >> 4 & 1) << 1 |
                                      bVar4 >> 5 & 1) << 1 | bVar4 >> 6 & 1) << 1 | bVar4 >> 7) << 8
                        | (uint)(byte)((((((((bVar1 & 1) << 1 | bVar1 >> 1 & 1) << 1 |
                                           bVar1 >> 2 & 1) << 1 | bVar1 >> 3 & 1) << 1 |
                                         bVar1 >> 4 & 1) << 1 | bVar1 >> 5 & 1) << 1 |
                                       bVar1 >> 6 & 1) << 1 | bVar1 >> 7));
      goto LAB_0001b80c;
    }
    uVar8 = LZCOUNT(param_4) - LZCOUNT(param_2);
    if (uVar8 < 0x1f) {
      uVar13 = uVar8 + 1;
      uVar6 = param_1 << (0x1f - uVar8 & 0xff);
      uVar11 = param_2 << (0x1f - uVar8 & 0xff) | param_1 >> (uVar13 & 0xff);
      uVar12 = param_2 >> (uVar13 & 0xff);
      uVar8 = 0;
      goto LAB_0001b904;
    }
LAB_0001b78c:
    if (param_5 != (uint *)0x0) {
      *param_5 = param_1;
      param_5[1] = param_2;
    }
  }
  param_2 = 0;
LAB_0001b80c:
  return (ulonglong)param_2;
}


// ==== FUN_0001ba2c @ 0001ba2c

void FUN_0001ba2c(uint param_1,uint param_2)

{
  if (param_2 == 0) {
    FUN_0001b724(0);
    return;
  }
  if (param_2 == 1) {
    return;
  }
  if (param_1 < param_2) {
    return;
  }
                    /* WARNING: Could not recover jumptable at 0x0001ba60. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*(code *)(&UNK_0001bbe8 + (LZCOUNT(param_2) - LZCOUNT(param_1)) * -0xc))();
  return;
}


// ==== dlsym @ 0001bc60

void dlsym(void)

{
  (*(code *)PTR_dlsym_0001cfd0)();
  return;
}


// ==== fopen @ 0001bc70

/* WARNING: Unknown calling convention -- yet parameter storage is locked */

FILE * fopen(char *__filename,char *__modes)

{
  FILE *pFVar1;
  
  pFVar1 = (FILE *)(*(code *)PTR_fopen_0001cfd4)(__filename);
  return pFVar1;
}


// ==== fgets @ 0001bc80

/* WARNING: Unknown calling convention -- yet parameter storage is locked */

char * fgets(char *__s,int __n,FILE *__stream)

{
  char *pcVar1;
  
  pcVar1 = (char *)(*(code *)PTR_fgets_0001cfd8)(__s);
  return pcVar1;
}


// ==== strstr @ 0001bc90

/* WARNING: Unknown calling convention -- yet parameter storage is locked */

char * strstr(char *__haystack,char *__needle)

{
  char *pcVar1;
  
  pcVar1 = (char *)(*(code *)PTR_strstr_0001cfdc)(__haystack);
  return pcVar1;
}


// ==== strtoul @ 0001bca0

/* WARNING: Unknown calling convention -- yet parameter storage is locked */

ulong strtoul(char *__nptr,char **__endptr,int __base)

{
  ulong uVar1;
  
  uVar1 = (*(code *)PTR_strtoul_0001cfe0)(__nptr);
  return uVar1;
}


// ==== fclose @ 0001bcb0

/* WARNING: Unknown calling convention -- yet parameter storage is locked */

int fclose(FILE *__stream)

{
  int iVar1;
  
  iVar1 = (*(code *)PTR_fclose_0001cfe4)(__stream);
  return iVar1;
}


// ==== __stack_chk_fail @ 0001bcc0

void __stack_chk_fail(void)

{
  (*(code *)PTR___stack_chk_fail_0001cfe8)();
  return;
}


// ==== dlopen @ 0001bcd0

void dlopen(void)

{
  (*(code *)PTR_dlopen_0001cfec)();
  return;
}


// ==== operator.new @ 0001bce0

/* WARNING: Unknown calling convention -- yet parameter storage is locked */

void * operator_new(uint param_1)

{
  void *pvVar1;
  
  pvVar1 = (void *)(*(code *)PTR_operator_new_0001cff0)(param_1);
  return pvVar1;
}


// ==== __aeabi_memclr4 @ 0001bcf0

void __aeabi_memclr4(void)

{
  (*(code *)PTR___aeabi_memclr4_0001cff4)();
  return;
}


// ==== strcmp @ 0001bd00

/* WARNING: Unknown calling convention -- yet parameter storage is locked */

int strcmp(char *__s1,char *__s2)

{
  int iVar1;
  
  iVar1 = (*(code *)PTR_strcmp_0001cff8)(__s1);
  return iVar1;
}


// ==== atoi @ 0001bd10

/* WARNING: Unknown calling convention -- yet parameter storage is locked */

int atoi(char *__nptr)

{
  int iVar1;
  
  iVar1 = (*(code *)PTR_atoi_0001cffc)(__nptr);
  return iVar1;
}


// ==== atof @ 0001bd20

/* WARNING: Unknown calling convention -- yet parameter storage is locked */

double atof(char *__nptr)

{
  undefined4 uVar1;
  undefined4 extraout_s1;
  
  uVar1 = (*(code *)PTR_atof_0001d000)(__nptr);
  return (double)CONCAT44(extraout_s1,uVar1);
}


// ==== __vsnprintf_chk @ 0001bd30

void __vsnprintf_chk(void)

{
  (*(code *)PTR___vsnprintf_chk_0001d004)();
  return;
}


// ==== __aeabi_memclr8 @ 0001bd40

void __aeabi_memclr8(void)

{
  (*(code *)PTR___aeabi_memclr8_0001d008)();
  return;
}


// ==== sscanf @ 0001bd50

/* WARNING: Unknown calling convention -- yet parameter storage is locked */

int sscanf(char *__s,char *__format,...)

{
  int iVar1;
  
  iVar1 = (*(code *)PTR_sscanf_0001d00c)(__s);
  return iVar1;
}


// ==== stbsp_vsprintfcb @ 0001bd60

void stbsp_vsprintfcb(void)

{
  (*(code *)PTR_stbsp_vsprintfcb_0001d010)();
  return;
}


// ==== __aeabi_memset @ 0001bd70

void __aeabi_memset(void)

{
  (*(code *)PTR___aeabi_memset_0001d014)();
  return;
}


// ==== __aeabi_memset4 @ 0001bd80

void __aeabi_memset4(void)

{
  (*(code *)PTR___aeabi_memset4_0001d018)();
  return;
}


// ==== __strncpy_chk2 @ 0001bd90

void __strncpy_chk2(void)

{
  (*(code *)PTR___strncpy_chk2_0001d01c)();
  return;
}


// ==== strncpy @ 0001bda0

/* WARNING: Unknown calling convention -- yet parameter storage is locked */

char * strncpy(char *__dest,char *__src,size_t __n)

{
  char *pcVar1;
  
  pcVar1 = (char *)(*(code *)PTR_strncpy_0001d020)(__dest);
  return pcVar1;
}


// ==== ModInfo @ 0001bdb0

void __thiscall
ModInfo::ModInfo(ModInfo *this,char *param_1,char *param_2,char *param_3,char *param_4)

{
  (*(code *)PTR_ModInfo_0001d024)(this);
  return;
}


// ==== stat @ 0001bdc0

/* WARNING: Unknown calling convention -- yet parameter storage is locked */

int stat(char *__file,stat *__buf)

{
  int iVar1;
  
  iVar1 = (*(code *)PTR_stat_0001d028)(__file);
  return iVar1;
}


// ==== __errno @ 0001bdd0

void __errno(void)

{
  (*(code *)PTR___errno_0001d02c)();
  return;
}


// ==== strncmp @ 0001bde0

/* WARNING: Unknown calling convention -- yet parameter storage is locked */

int strncmp(char *__s1,char *__s2,size_t __n)

{
  int iVar1;
  
  iVar1 = (*(code *)PTR_strncmp_0001d030)(__s1);
  return iVar1;
}


// ==== __strncpy_chk @ 0001bdf0

void __strncpy_chk(void)

{
  (*(code *)PTR___strncpy_chk_0001d034)();
  return;
}


// ==== __cxa_guard_acquire @ 0001be00

void __cxa_guard_acquire(void)

{
  (*(code *)PTR___cxa_guard_acquire_0001d038)();
  return;
}


// ==== __cxa_guard_release @ 0001be10

void __cxa_guard_release(void)

{
  (*(code *)PTR___cxa_guard_release_0001d03c)();
  return;
}


// ==== __vsprintf_chk @ 0001be20

void __vsprintf_chk(void)

{
  (*(code *)PTR___vsprintf_chk_0001d040)();
  return;
}


// ==== operator.delete @ 0001be30

/* WARNING: Unknown calling convention -- yet parameter storage is locked */

void operator_delete(void *param_1)

{
  (*(code *)PTR_operator_delete_0001d044)(param_1);
  return;
}


// ==== strlen @ 0001be40

/* WARNING: Unknown calling convention -- yet parameter storage is locked */

size_t strlen(char *__s)

{
  size_t sVar1;
  
  sVar1 = (*(code *)PTR_strlen_0001d048)(__s);
  return sVar1;
}


// ==== __strcpy_chk @ 0001be50

void __strcpy_chk(void)

{
  (*(code *)PTR___strcpy_chk_0001d04c)();
  return;
}


// ==== __cxa_finalize @ 0001f000

/* WARNING: Control flow encountered bad instruction data */

void __cxa_finalize(void)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== __cxa_atexit @ 0001f004

/* WARNING: Control flow encountered bad instruction data */

void __cxa_atexit(void)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== __register_atfork @ 0001f008

/* WARNING: Control flow encountered bad instruction data */

void __register_atfork(void)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== dlsym @ 0001f00c

/* WARNING: Control flow encountered bad instruction data */

void dlsym(void)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== fopen @ 0001f010

/* WARNING: Control flow encountered bad instruction data */
/* WARNING: Unknown calling convention -- yet parameter storage is locked */

FILE * fopen(char *__filename,char *__modes)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== fgets @ 0001f014

/* WARNING: Control flow encountered bad instruction data */
/* WARNING: Unknown calling convention -- yet parameter storage is locked */

char * fgets(char *__s,int __n,FILE *__stream)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== strstr @ 0001f018

/* WARNING: Control flow encountered bad instruction data */
/* WARNING: Unknown calling convention -- yet parameter storage is locked */

char * strstr(char *__haystack,char *__needle)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== strtoul @ 0001f01c

/* WARNING: Control flow encountered bad instruction data */
/* WARNING: Unknown calling convention -- yet parameter storage is locked */

ulong strtoul(char *__nptr,char **__endptr,int __base)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== fclose @ 0001f020

/* WARNING: Control flow encountered bad instruction data */
/* WARNING: Unknown calling convention -- yet parameter storage is locked */

int fclose(FILE *__stream)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== __stack_chk_fail @ 0001f024

/* WARNING: Control flow encountered bad instruction data */

void __stack_chk_fail(void)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== dlopen @ 0001f02c

/* WARNING: Control flow encountered bad instruction data */

void dlopen(void)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== operator.new @ 0001f030

/* WARNING: Control flow encountered bad instruction data */
/* WARNING: Unknown calling convention -- yet parameter storage is locked */

void * operator_new(uint param_1)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== __aeabi_memclr4 @ 0001f034

/* WARNING: Control flow encountered bad instruction data */

void __aeabi_memclr4(void)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== strcmp @ 0001f038

/* WARNING: Control flow encountered bad instruction data */
/* WARNING: Unknown calling convention -- yet parameter storage is locked */

int strcmp(char *__s1,char *__s2)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== atoi @ 0001f03c

/* WARNING: Control flow encountered bad instruction data */
/* WARNING: Unknown calling convention -- yet parameter storage is locked */

int atoi(char *__nptr)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== atof @ 0001f040

/* WARNING: Control flow encountered bad instruction data */
/* WARNING: Unknown calling convention -- yet parameter storage is locked */

double atof(char *__nptr)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== __vsnprintf_chk @ 0001f044

/* WARNING: Control flow encountered bad instruction data */

void __vsnprintf_chk(void)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== __aeabi_memclr8 @ 0001f048

/* WARNING: Control flow encountered bad instruction data */

void __aeabi_memclr8(void)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== sscanf @ 0001f04c

/* WARNING: Control flow encountered bad instruction data */
/* WARNING: Unknown calling convention -- yet parameter storage is locked */

int sscanf(char *__s,char *__format,...)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== __aeabi_memset @ 0001f050

/* WARNING: Control flow encountered bad instruction data */

void __aeabi_memset(void)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== __aeabi_memset4 @ 0001f054

/* WARNING: Control flow encountered bad instruction data */

void __aeabi_memset4(void)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== __strncpy_chk2 @ 0001f058

/* WARNING: Control flow encountered bad instruction data */

void __strncpy_chk2(void)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== strncpy @ 0001f05c

/* WARNING: Control flow encountered bad instruction data */
/* WARNING: Unknown calling convention -- yet parameter storage is locked */

char * strncpy(char *__dest,char *__src,size_t __n)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== stat @ 0001f060

/* WARNING: Control flow encountered bad instruction data */
/* WARNING: Unknown calling convention -- yet parameter storage is locked */

int stat(char *__file,stat *__buf)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== __errno @ 0001f064

/* WARNING: Control flow encountered bad instruction data */

void __errno(void)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== strncmp @ 0001f068

/* WARNING: Control flow encountered bad instruction data */
/* WARNING: Unknown calling convention -- yet parameter storage is locked */

int strncmp(char *__s1,char *__s2,size_t __n)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== __strncpy_chk @ 0001f06c

/* WARNING: Control flow encountered bad instruction data */

void __strncpy_chk(void)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== __cxa_guard_acquire @ 0001f070

/* WARNING: Control flow encountered bad instruction data */

void __cxa_guard_acquire(void)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== __cxa_guard_release @ 0001f074

/* WARNING: Control flow encountered bad instruction data */

void __cxa_guard_release(void)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== __vsprintf_chk @ 0001f078

/* WARNING: Control flow encountered bad instruction data */

void __vsprintf_chk(void)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== operator.delete @ 0001f07c

/* WARNING: Control flow encountered bad instruction data */
/* WARNING: Unknown calling convention -- yet parameter storage is locked */

void operator_delete(void *param_1)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== strlen @ 0001f080

/* WARNING: Control flow encountered bad instruction data */
/* WARNING: Unknown calling convention -- yet parameter storage is locked */

size_t strlen(char *__s)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== __strcpy_chk @ 0001f084

/* WARNING: Control flow encountered bad instruction data */

void __strcpy_chk(void)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}


// ==== STRINGS
// 0001063d __cxa_finalize
// 0001064c __cxa_atexit
// 00010659 __register_atfork
// 0001066b dlsym
// 00010671 fopen
// 00010677 fgets
// 0001067d strstr
// 00010684 strtoul
// 0001068c fclose
// 00010693 __stack_chk_fail
// 000106a4 __stack_chk_guard
// 000106b6 dlopen
// 000106bd _Znwj
// 000106c3 __aeabi_memclr4
// 000106d3 strcmp
// 000106da atoi
// 000106df atof
// 000106e4 __vsnprintf_chk
// 000106f4 __aeabi_memclr8
// 00010704 sscanf
// 0001070b stbsp_set_separators
// 00010720 stbsp_vsprintfcb
// 00010731 __aeabi_memset
// 00010740 __aeabi_memset4
// 00010750 stbsp_sprintf
// 0001075e stbsp_vsnprintf
// 0001076e stbsp_snprintf
// 0001077d stbsp_vsprintf
// 0001078c __strncpy_chk2
// 0001079b strncpy
// 000107a3 _ZN7ModInfoC2EPKcS1_S1_S1_
// 000107be __GetModInfo
// 000107cb modinfo
// 000107d3 stat
// 000107d8 __errno
// 000107e0 OnModLoad
// 000107ea sautils
// 000107f2 strncmp
// 000107fa __strncpy_chk
// 00010808 __cxa_guard_acquire
// 0001081c __cxa_guard_release
// 00010830 __vsprintf_chk
// 0001083f amlStub
// 00010847 _ZdlPv
// 0001084e strlen
// 00010855 __strcpy_chk
// 00010862 libc.so
// 0001086a LIBC
// 0001086f LIBC_N
// 00010876 libstdc++.so
// 00010883 LIBC_O
// 0001088a libdl.so
// 00010893 liblog.so
// 0001089d libm.so
// 000108a5 libAdvancedSettings.SA.so
// 00011468 HUD_PlayerInfo
// 00011477 krose
// 0001147d mastersounds
// 0001148a _ZN5CFont14GetStringWidthEPthhh
// 000114aa AERadioTrackManager
// 000114c1 _kMGT
// 000114cb menu_mainsettings
// 000114dd Discord
// 000114e5 HUD_BarArmor
// 000114f2 SizeX
// 000114f8 HUD_WeaponIcon
// 00011507 ColorMinus
// 00011512 HUD_Radar
// 0001151c TPLAYER
// 00011524 HUD_WantedStar
// 00011533 _ZN5CFont8SetColorE5CRGBA
// 0001154d /proc/self/maps
// 0001155d GetInterface
// 0001156a LEGEND
// 00011571 _ZN10MobileMenu6RenderEv
// 0001158f gGxtString
// 0001159a DisplayTime
// 000115a6 ForceDrawing
// 000115b3 _ZN5CFont7DetailsE
// 000115c6 HUD_MoneyChange
// 000115da _ZN5CFont6SpriteE
// 000115ec _ZN4CHud13m_ItemToFlashE
// 00011605 libAML.so
// 0001160f HUD Mode
// 00011618 _ZN5CFont14SetOrientationEh
// 00011634 _ZN5CFont11PrintStringEffPt
// 00011650 _ZN6CRadar12m_radarRangeE
// 0001166a 0123456789abcdefxp
// 0001167d Advanced Settings
// 0001168f Widget Alpha
// 0001169c v7.3.1 PBLC
// 000116a8 Author
// 000116af AnimatedMult
// 000116bc DropShadow
// 000116c7 _ZN5CFont7SetEdgeEa
// 000116db _ZN6CStats12GetStatValueEt
// 000116f6 WGTALPH
// 000116ff Enable
// 00011706 _ZN5CFont19SetRightJustifyWrapEf
// 0001172c BLIPS ONLY
// 00011737 DrawBehind
// 00011742 PlayerPosX
// 00011752 _ZN9CSprite2d8DrawRectERK5CRectRK5CRGBA
// 0001177a ARM [ Vanilla like ]
// 0001178f Color
// 0001179a _ZN5CFont19UsingKoreanLanguageE
// 000117ba _ZN6CWorld7PlayersE
// 000117ce _Z13RwTextureReadPKcS0_
// 000117e6 ARM: touch me >.<
// 000117f8 PCBehaviour
// 00011804 CalcWeaponRect
// 00011813 HUD_Money
// 0001181d bounce
// 00011824 _ZN6CTimer12ms_fTimeStepE
// 0001183e MAP & BLIPS
// 0001184a Version
// 00011852 DrawMode
// 0001185b SizeY
// 00011866 _ZN5CFont16RenderFontBufferEv
// 00011884 _ZN6CWorld13PlayerInFocusE
// 0001189f _ZN4CHud23bScriptDontDisplayRadarE
// 000118c2 gMobileMenu
// 000118d3 1.5.1
// 000118d9 FlashItem
// 000118e3 HUD_BarHealth
// 000118f1 RADIOX
// 000118f8 radio_%s
// 00011901 EnableIcon
// 0001190c _ZN17CWidgetPlayerInfo10DrawWantedEv
// 00011931 _Z16RwRenderStateSet13RwRenderStatePv
// 0001195b _ZN5CFont8SetScaleEf
// 00011970 %f %f %f %f
// 0001197c %hu.%hu
// 00011984 ConstWidth
// 0001198f HUD_Clock
// 0001199e HUD_VehName
// 000119ad FEM_OFF
// 000119ba Legend Bg Shadow
// 000119cb _Z12AND_OpenLinkPKc
// 000119df Vertical
// 000119e8 FontStyle
// 000119f2 ColorPlus
// 00011a01 HUD_Radio
// 00011a0b %d %d %d %d
// 00011a1d Radar Mode
// 00011a2d Hi cutie, Advanced Settings need AML v1.4.0
// 00011a59 BorderMult
// 00011a64 HUD_BarBreath
// 00011a72 _ZN5CFont18UsingEFIGSLanguageE
// 00011a91 Touch Sensitivity Y
// 00011aaa %s%s.ini
// 00011ab3 About
// 00011ac6 FontEdge
// 00011ad3 _ZN5CFont21UsingJapaneseLanguageE
// 00011af5 _ZN6CTimer22m_snTimeInMillisecondsE
// 00011b1d LBGSHDW
// 00011b25 GOVRLAY
// 00011b2d net.rusjj.gtasa.utils
// 00011b43 _ZN17CWidgetPlayerInfo15RenderBreathBarEif
// 00011b70 _ZN5CFont15SetProportionalEh
// 00011b8d _ZN9CSprite2d4DrawEffffRK5CRGBA
// 00011bb2 Widget
// 00011bc3 PresetBy
// 00011bcc %d-%d
// 00011bd2 BGColor
// 00011bda deviceForm
// 00011be5 _Z17FindPlayerVehicleib
// 00011bfd _Z20GxtCharMakeUpperCasePtPKt
// 00011c1e https://youtube.com/@arm_chxd
// 00011c3c _ZN17CWidgetPlayerInfo14DrawWeaponIconEP4CPed5CRectf
// 00011c71 _ZN17CWidgetPlayerInfo4DrawEv
// 00011c8f Proportional
// 00011c9c %02d:%02d
// 00011cab _ZN5CFont12SetDropColorE5CRGBA
// 00011cca _Z14AsciiToGxtCharPKcPt
// 00011ce2 AMLConfig
// 00011cec AML Mod
// 00011cf8 HUD Alpha
// 00011d02 _ZN17CWidgetPlayerInfo14RenderArmorBarEif
// 00011d2c AnimSpeed
// 00011d36 _ZN5CFont8SetWrapxEf
// 00011d4b RsGlobal
// 00011d56 HUDALPH
// 00011d5e https://raw.githubusercontent.com/ARAH64/Updater-mods/main/ADVS/news.txt
// 00011da7 Orientation
// 00011db7 HUD_Subtitles
// 00011dc5 _ZN5CFont20UsingRussianLanguageE
// 00011de6 Legend
// 00011ded %hu.%hu.%hu.%hu
// 00011dfd AMLInterface
// 00011e0a https://discord.gg/rtBfy5AWSE
// 00011e2d HUD_Ammo
// 00011e36 _ZN12CWidgetRadar6UpdateEv
// 00011e51 _ZN7CWidget14m_fElapsedTimeE
// 00011e6e FEM_ON
// 00011e75 WGTALL
// 00011e7c net.rusjj.aml
// 00011e8a 1.3.0
// 00011e90 AlwaysShow
// 00011e9b Padding
// 00011ea3 _ZN6CStats23GetFatAndMuscleModifierE17eStatModAbilities
// 00011edb 0123456789ABCDEFXP
// 00011eee Channel
// 00011efb SENSX
// 00011f01 %hu.%hu.%hu
// 00011f0d ARM Advanced Settings %s
// 00011f2b DisplayAbove9999
// 00011f3c AnimStart
// 00011f46 Range
// 00011f4c IsInitialized
// 00011f5a libGTASA.so
// 00011f66 Format
// 00011f6d playback
// 00011f76 _ZN5CFont10SetJustifyEh
// 00011f8e _KMGT
// 00011f94 GTA:SA Advanced Settings
// 00011fad 7.3.1 PBLC
// 00011fb8 RADAR
// 00011fbe _ZN7CWidget11ManageAlphaEv
// 00011fd9 SAUtils
// 00011fe1 No Widget Exception
// 00011ff5 BorderSide
// 00012000 PlayerPosY
// 0001200b AlwaysShowShadow
// 0001201c _ZN5CFont21SetDropShadowPositionEa
// 0001203f _Z16FindPlayerWantedi
// 00012055 AdvancedSettings.SA
// 00012069 WIDGET
// 00012070 SENSY
// 00012076 Gang Overlay
// 00012083 Touch Sensitivity X
// 00012097 _ZN17CWidgetPlayerInfo15RenderHealthBarEif
// 000120c6 Percent
// 000120ce HUD_AreaName
// 000120db _ZN5CFont12SetFontStyleEh
// 000120f5 _ZN5CFont13SetBackgroundEhh
// 00012114 00010203040506070809101112131415161718192021222324252627282930313233343536373839404142434445464748495051525354555657585960616263646566676869707172737475767778798081828384858687888990919293949596979899
// .comment::00000000 Linker: LLD 20.0.0 (/mnt/disks/build-disk/src/android/llvm-r547379-release/out/llvm-project/llvm b718bcaf8c198c82f3021447d943401e3ab5bd54)
// .comment::0000008b Android (13065274, based on r547379) clang version 20.0.0 (https://android.googlesource.com/toolchain/llvm-project b718bcaf8c198c82f3021447d943401e3ab5bd54)
// .comment::00000128 
// .comment::00000129 Android (13065274, +pgo, +bolt, +lto, +mlgo, based on r547379) clang version 20.0.0 (https://android.googlesource.com/toolchain/llvm-project b718bcaf8c198c82f3021447d943401e3ab5bd54)
// .shstrtab::00000001 .init_array
// .shstrtab::0000000d .fini_array
// .shstrtab::00000019 .ARM.exidx
// .shstrtab::00000024 .text
// .shstrtab::0000002a .got
// .shstrtab::0000002f .comment
// .shstrtab::00000038 .note.android.ident
// .shstrtab::0000004c .got.plt
// .shstrtab::00000055 .rel.plt
// .shstrtab::0000005e .bss
// .shstrtab::00000063 .ARM.attributes
// .shstrtab::00000073 .dynstr
// .shstrtab::0000007b .gnu.version_r
// .shstrtab::0000008a .data.rel.ro
// .shstrtab::00000097 .rel.dyn
// .shstrtab::000000a0 .gnu.version
// .shstrtab::000000ad .dynsym
// .shstrtab::000000b5 .gnu.hash
// .shstrtab::000000bf .relro_padding
// .shstrtab::000000ce .note.gnu.build-id
// .shstrtab::000000e1 .dynamic
// .shstrtab::000000ea .ARM.extab
// .shstrtab::000000f5 .shstrtab
// .shstrtab::000000ff .rodata
// .shstrtab::00000107 .data
