#!/bin/bash
set -euo pipefail
cache=$1
out=$2
tfa_cache=${3:-}
root=$(cd "$(dirname "$0")/../../.." && pwd)
bin=$(mktemp -d)/osrs_export
cc -std=c11 -O2 -fwrapv -D_DEFAULT_SOURCE -I"$root" -o "$bin" "$root/ocean/osrs/tools/osrs_export.c" -lz -lbz2 -lm
mkdir -p "$out"
x() { "$bin" "$cache" "$out" "$@"; }

x scene zulrah 35,47 35,48
x scene inferno 35,83 altobjects=inferno_zuk:30327,30328,30329,30330,30332,30333,30334,30336,30337,30338
x scene colosseum 28,48
x scene wilderness 47,55 48,55
x collision wilderness 44,48 56,62

x npcs zulrah npc=2042:5068 npc=2043:5806 npc=2044:5068 npc=2045:140 npc=2046:185 \
  gfx=1044 gfx=1045 gfx=1046 gfx=1047 gfx=1230 loc=11700 \
  seq=5069 seq=5071 seq=5072 seq=5073 seq=5804 seq=5807 seq=5808 seq=138

x npcs inferno npc=7691:7574 npc=7692:7578 npc=7693:7581 npc=7694 npc=7695 npc=7696 \
  npc=7697:7597 npc=7698:7605 npc=7699:7610 npc=7700:7593 npc=7701 npc=7706:7566 npc=7707 npc=7708 \
  gfx=157 gfx=448 gfx=449 gfx=450 gfx=451 gfx=659 gfx=660 gfx=1120 gfx=1374 gfx=1375 gfx=1376 gfx=1377 \
  gfx=1378 gfx=1379 gfx=1380 gfx=1381 gfx=1382 gfx=1383 gfx=1384 gfx=1385 \
  loc=30284 loc=30285 loc=30286 loc=30287 \
  seq=7575 seq=7576 seq=7579 seq=7580 seq=7582 seq=7583 seq=7584 seq=7585 seq=7598 seq=7599 seq=7600 \
  seq=7601 seq=7604 seq=7606 seq=7607 seq=7611 seq=7612 seq=7613 seq=7590 seq=7591 seq=7592 seq=7594 \
  seq=7562 seq=7563 seq=7565 seq=7568 seq=7569

x npcs colosseum npc=10880:11101:11595 npc=12810:10847:10848 npc=12811:10859:10860 npc=12812:10843:10845 \
  npc=12813:10843:10845 npc=12814:10850:10851 npc=12815:10853:10854 npc=12816:10856:10857 \
  npc=12817:10892:10894:10893 npc=12818:10869:10866:10868 npc=12819:10903:10895 \
  npc=12821:10883:10888:10882:10884:10885:10886:10887 npc=12823:10823 npc=12824 npc=12825:10828 \
  npc=12826:-1:10817 npc=12834:-1:10872 npc=12835:-1:10872 npc=12836:-1:10872 seqmodel=10896:52586 \
  seq=693 seq=7856 seq=7857 seq=10327 seq=10328 seq=10329 seq=10330 seq=10803 seq=10804 seq=10805 \
  seq=10806 seq=10807 seq=10808 seq=10809 seq=10810 seq=10811 seq=10812 seq=10900 seq=10901

x textures

if [ -n "$tfa_cache" ]; then
  t() { OSRS_GAMEVAL_TSV="$root/ocean/osrs/tools/tfa_gameval.tsv" "$bin" "$tfa_cache" "$out" "$@"; }

  t scene tfa_guardian 64,87
  t scene tfa_ezekiel 63,86
  t scene tfa_dagonhai 63,87
  t scene tfa_moia 63,88
  t scene tfa_bork 65,87
  t scene tfa_dreadnoth 65,88
  t scene tfa_xarth 65,86
  t scene tfa_ocellus 64,88 plane=1
  t scene tfa_nexus 64,87 plane=2

  t npcs tfa_bork npc=16642 \
    npc=16640:14542:14538:14539:14540:14541:14542:14543:14544:14545:14546:14547:14548:14549:14550:14551:14552:14556:14557 \
    npc=16641:14554:14555 gfx=4036 gfx=4037 gfx=4038 gfx=4039 gfx=4040 gfx=4041 gfx=4042 gfx=4043 gfx=4044 \
    gfx=4045 gfx=4046 gfx=4047 gfx=4048 gfx=4049 gfx=4050 gfx=4051 gfx=4052 gfx=4053 gfx=4054 gfx=4055 \
    gfx=4056 gfx=4057 gfx=4058 gfx=4059 gfx=4060 gfx=4061 gfx=4062 gfx=4388 gfx=4389 gfx=4390 gfx=4391 \
    gfx=4392 gfx=4393 gfx=4394 gfx=4395 gfx=4396 gfx=4397 gfx=4398 gfx=4399 gfx=4400 gfx=4401 gfx=4402 \
    gfx=4403 gfx=4404 gfx=4405 gfx=4418 gfx=4419 gfx=4420 gfx=4421 gfx=4440 loc=62544 loc=62545 loc=62546 \
    loc=62547 loc=62548 loc=62549 loc=62550 loc=62551

  t npcs tfa_guardian npc=16657:-1:14755 npc=16658 \
    npc=16659:14751:14746:14747:14748:14749:14750:14752:14753:14754 npc=16660:14752 npc=16661 \
    npc=16662:-1:15025 npc=16663 npc=16664:14768:14769:14771 gfx=4199 gfx=4200 gfx=4201 gfx=4202 gfx=4203 \
    gfx=4204 gfx=4205 gfx=4206 gfx=4207 gfx=4208 gfx=4388 gfx=4389 gfx=4390 gfx=4391 gfx=4392 gfx=4393 \
    gfx=4394 gfx=4395 gfx=4396 gfx=4397 gfx=4398 gfx=4399 gfx=4400 gfx=4401 gfx=4402 gfx=4403 gfx=4404 \
    gfx=4405 gfx=4418 gfx=4419 gfx=4420 gfx=4421 loc=62578 loc=62579 loc=62580 seq=8631 seq=8632 seq=14756

  t npcs tfa_dagonhai npc=16643:14593:14597:14604:14603:14607:14609:14611:14622:14623:14624:14625:14626:14652 \
    npc=16644 npc=16645:14599:14612:14614:14616:14630:14631:14632:14633:14634:14653 npc=16646 \
    npc=16647:14601:14617:14619:14621:14654 npc=16648 npc=16649:14657:14659:14661:14678:14679 npc=16650 \
    npc=16651 gfx=4063 gfx=4064 gfx=4065 gfx=4066 gfx=4067 gfx=4068 gfx=4069 gfx=4070 gfx=4071 gfx=4072 \
    gfx=4073 gfx=4074 gfx=4075 gfx=4076 gfx=4077 gfx=4078 gfx=4079 gfx=4080 gfx=4081 gfx=4082 gfx=4083 \
    gfx=4084 gfx=4085 gfx=4086 gfx=4087 gfx=4088 gfx=4089 gfx=4090 gfx=4091 gfx=4092 gfx=4093 gfx=4094 \
    gfx=4095 gfx=4096 gfx=4097 gfx=4098 gfx=4099 gfx=4100 gfx=4101 gfx=4102 gfx=4103 gfx=4104 gfx=4105 \
    gfx=4106 gfx=4107 gfx=4108 gfx=4109 gfx=4110 gfx=4111 gfx=4112 gfx=4113 gfx=4114 gfx=4115 gfx=4116 \
    gfx=4117 gfx=4118 gfx=4119 gfx=4120 gfx=4121 gfx=4122 gfx=4123 gfx=4124 gfx=4125 gfx=4126 gfx=4127 \
    gfx=4128 gfx=4129 gfx=4130 gfx=4131 gfx=4132 gfx=4133 gfx=4134 gfx=4135 gfx=4136 gfx=4137 gfx=4138 \
    gfx=4139 gfx=4140 gfx=4141 gfx=4142 gfx=4143 gfx=4144 gfx=4145 gfx=4146 gfx=4147 gfx=4148 gfx=4149 \
    gfx=4150 gfx=4151 gfx=4152 gfx=4153 gfx=4154 gfx=4155 gfx=4156 gfx=4157 gfx=4158 gfx=4159 gfx=4160 \
    gfx=4161 gfx=4162 gfx=4163 gfx=4164 gfx=4165 gfx=4166 gfx=4388 gfx=4389 gfx=4390 gfx=4391 gfx=4392 \
    gfx=4393 gfx=4394 gfx=4395 gfx=4396 gfx=4397 gfx=4398 gfx=4399 gfx=4400 gfx=4401 gfx=4402 gfx=4403 \
    gfx=4404 gfx=4405 gfx=4418 gfx=4419 gfx=4420 gfx=4421 loc=62552 loc=62553 loc=62576 seq=14681

  t npcs tfa_ezekiel npc=16652:14685:14686:14687:14688:14689:14691:14692:14693:14694:14705:14706 npc=16653 \
    npc=16654:-1:14730:14731:14732:14733:14734:14735:14736:14737:14738:14739:14740:14741:14742:14743:14744 \
    npc=16655 npc=16656 gfx=4167 gfx=4168 gfx=4169 gfx=4170 gfx=4171 gfx=4172 gfx=4174 gfx=4175 gfx=4176 \
    gfx=4177 gfx=4178 gfx=4179 gfx=4180 gfx=4181 gfx=4182 gfx=4183 gfx=4184 gfx=4185 gfx=4186 gfx=4187 \
    gfx=4188 gfx=4189 gfx=4190 gfx=4191 gfx=4192 gfx=4193 gfx=4194 gfx=4195 gfx=4196 gfx=4197 gfx=4198 \
    gfx=4388 gfx=4389 gfx=4390 gfx=4391 gfx=4392 gfx=4393 gfx=4394 gfx=4395 gfx=4396 gfx=4397 gfx=4398 \
    gfx=4399 gfx=4400 gfx=4401 gfx=4402 gfx=4403 gfx=4404 gfx=4405 gfx=4418 gfx=4419 gfx=4420 gfx=4421

  t npcs tfa_dreadnoth npc=16665:14779:14777:14780:14782 npc=16666:1586:1587:14773:14774:15162 gfx=4209 \
    gfx=4210 gfx=4211 gfx=4212 gfx=4388 gfx=4389 gfx=4390 gfx=4391 gfx=4392 gfx=4393 gfx=4394 gfx=4395 \
    gfx=4396 gfx=4397 gfx=4398 gfx=4399 gfx=4400 gfx=4401 gfx=4402 gfx=4403 gfx=4404 gfx=4405 gfx=4418 \
    gfx=4419 gfx=4420 gfx=4421 loc=62609 loc=63122 loc=63123 loc=63124 loc=63125 loc=63112 loc=63126 loc=63127 \
    loc=63128 loc=63129 loc=63130 loc=63134 seq=15023

  t npcs tfa_moia npc=16667:14785:14784:14786:14787:14788 npc=16670:14791:14792 gfx=4213 gfx=4214 gfx=4216 \
    gfx=4217 gfx=4218 gfx=4219 gfx=4220 gfx=4221 gfx=4222 gfx=4223 gfx=4224 gfx=4225 gfx=4388 gfx=4389 \
    gfx=4390 gfx=4391 gfx=4392 gfx=4393 gfx=4394 gfx=4395 gfx=4396 gfx=4397 gfx=4398 gfx=4399 gfx=4400 \
    gfx=4401 gfx=4402 gfx=4403 gfx=4404 gfx=4405 gfx=4418 gfx=4419 gfx=4420 gfx=4421 loc=62610 loc=62611 \
    loc=62612 loc=62613 loc=62614 loc=62631 loc=62632 loc=62633 loc=62634 seq=15023

  t npcs tfa_xarth npc=16684:14985:14981:14982:14983:14984:14989:14991:14992:14995:15017:15018 npc=16685 \
    npc=16686:-1:14997 npc=16687:-1:15003:15005 npc=16688 gfx=4034 gfx=4373 gfx=4374 gfx=4375 gfx=4376 \
    gfx=4377 gfx=4378 gfx=4379 gfx=4380 gfx=4381 gfx=4382 gfx=4383 gfx=4384 gfx=4385 gfx=4386 gfx=4387 \
    gfx=4388 gfx=4389 gfx=4390 gfx=4391 gfx=4392 gfx=4393 gfx=4394 gfx=4395 gfx=4396 gfx=4397 gfx=4398 \
    gfx=4399 gfx=4400 gfx=4401 gfx=4402 gfx=4403 gfx=4404 gfx=4405 gfx=4418 gfx=4419 gfx=4420 gfx=4421 \
    loc=62710 loc=62711 loc=62712 loc=62713 loc=62747 loc=62748 loc=62749 loc=62750 loc=62751 loc=62752 \
    loc=62753 loc=62754 loc=62755 loc=62756 loc=62757 loc=62758 loc=62759 loc=62760 loc=62761 loc=62762 \
    loc=62763 loc=62764 seq=12005 seq=15008 seq=15009 seq=15010 seq=15011 seq=15012 seq=15013 seq=15014 \
    seq=15015

  t npcs tfa_ocellus npc=16669 npc=16672 npc=16673 \
    npc=16674:14806:14807:14808:14809:14810:14811:14812:14813:14814 npc=16675:-1:14837:14878 npc=16676 \
    npc=16677:14842:14841:14843:14844:14845:14846:14847:14848:14850:14851:14852:14853:14854:14855:14856:14857:14858:14859:14860:14861:14862:14863:14864:14865:14866:14867:14868:14869:14870:14871:14872:14881 \
    npc=16678:-1:14896:14897:14898 npc=16679:-1:14900:14901:14902 npc=16680:-1:14904:14905:14906 npc=16681 \
    npc=16683 gfx=4226 gfx=4227 gfx=4228 gfx=4229 gfx=4230 gfx=4231 gfx=4232 gfx=4233 gfx=4234 gfx=4236 \
    gfx=4237 gfx=4238 gfx=4239 gfx=4240 gfx=4241 gfx=4242 gfx=4243 gfx=4244 gfx=4245 gfx=4246 gfx=4247 \
    gfx=4248 gfx=4249 gfx=4250 gfx=4251 gfx=4252 gfx=4253 gfx=4254 gfx=4255 gfx=4256 gfx=4257 gfx=4258 \
    gfx=4259 gfx=4260 gfx=4261 gfx=4262 gfx=4263 gfx=4264 gfx=4265 gfx=4266 gfx=4267 gfx=4268 gfx=4269 \
    gfx=4270 gfx=4271 gfx=4272 gfx=4273 gfx=4274 gfx=4275 gfx=4276 gfx=4277 gfx=4278 gfx=4279 gfx=4280 \
    gfx=4281 gfx=4282 gfx=4283 gfx=4284 gfx=4285 gfx=4286 gfx=4287 gfx=4288 gfx=4290 gfx=4291 gfx=4292 \
    gfx=4293 gfx=4294 gfx=4295 gfx=4296 gfx=4297 gfx=4298 gfx=4299 gfx=4300 gfx=4301 gfx=4302 gfx=4303 \
    gfx=4304 gfx=4305 gfx=4306 gfx=4308 gfx=4309 gfx=4310 gfx=4311 gfx=4312 gfx=4313 gfx=4314 gfx=4315 \
    gfx=4316 gfx=4317 gfx=4318 gfx=4319 gfx=4320 gfx=4321 gfx=4322 gfx=4323 gfx=4324 gfx=4325 gfx=4326 \
    gfx=4327 gfx=4328 gfx=4329 gfx=4330 gfx=4331 gfx=4332 gfx=4333 gfx=4334 gfx=4335 gfx=4336 gfx=4337 \
    gfx=4338 gfx=4339 gfx=4340 gfx=4346 gfx=4347 gfx=4348 gfx=4349 gfx=4350 gfx=4351 gfx=4352 gfx=4353 \
    gfx=4354 gfx=4355 gfx=4356 gfx=4357 gfx=4358 gfx=4359 gfx=4360 gfx=4361 gfx=4362 gfx=4363 gfx=4364 \
    gfx=4365 gfx=4366 gfx=4367 gfx=4368 gfx=4369 gfx=4370 gfx=4371 gfx=4372 gfx=4388 gfx=4389 gfx=4390 \
    gfx=4391 gfx=4392 gfx=4393 gfx=4394 gfx=4395 gfx=4396 gfx=4397 gfx=4398 gfx=4399 gfx=4400 gfx=4401 \
    gfx=4402 gfx=4403 gfx=4404 gfx=4405 gfx=4418 gfx=4419 gfx=4420 gfx=4421 loc=62684 loc=62685 loc=62686 \
    loc=62687 loc=62688 loc=62689 loc=62690 loc=62691 loc=62692 loc=62693 loc=62694 loc=62695 loc=62696 \
    loc=62697 loc=62698 loc=62699 loc=62700 loc=62701 loc=62702 loc=62703 loc=62704 loc=62705 loc=63000 \
    loc=63001 loc=63002 loc=63003 loc=63004 loc=63005 loc=63006 loc=63007 loc=63008 loc=63109 loc=63110 \
    loc=63111 seq=14830 seq=14833 seq=14834 seq=14891 seq=14924 seq=14927 seq=14930 seq=14936 seq=14970 \
    seq=14977 seq=14978 seq=15020 seq=15022

  t npcs tfa_nexus npc=16761 npc=16762 loc=63087 loc=63088 loc=63089 loc=63090 loc=63091 loc=63092 loc=63093 \
    loc=63094 loc=63095 loc=63096 loc=63097 loc=63098 loc=63099 loc=63100 loc=63101 loc=63102 loc=63103 \
    loc=63104 loc=63105 loc=63106 loc=63107 loc=63108 loc=63109 loc=63110 loc=63111 seq=15020 seq=15021 \
    seq=15022 seq=15023

  t textures
fi

x sprites
x interfaces
x fonts
x projectiles
x equipment
x icons
