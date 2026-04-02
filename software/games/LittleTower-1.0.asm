;       Little Tower v1.0
; Apple1 Adventure Game
;    	Written by VERHILLE Arnaud
;		(C) September 2000
;       Updated April 2026

;********************************************
;*********** MACROS DEFINITIONS *************
;********************************************

.macpack longbranch   ;import standard  macro def

;*** Test String macro ***
;Return Accumulator Value : $00 false, $FF true
        .macro  testString StringAddress
                LDA #<StringAddress
		STA AddressL
		LDA #>StringAddress
		STA AddressH
		JSR COMPARE
        .endmacro

;*** Print String macro ***
        .macro printString StringAddress
                LDA #<StringAddress
		STA AddressL
		LDA #>StringAddress
		STA AddressH
		JSR READTXT
        .endmacro

;*** Print Room Information macro ***
        .macro printRoomInfo RoomNumbTxt,RoomNumbObj,RoomNumbExit
                printString RoomNumbTxt
		printString ObjTxt
		printString RoomNumbObj
		printString ExitTxt
		printString RoomNumbExit
        .endmacro

;********************************************
;**************** VARIABLES *****************
;********************************************

.org $0300	; Type 300R to run

;******* Hardware Variables ************

KBD   = $D010	         ; Keyboard I/O
KBDCR = $D011

;************ Bios Address *************

ECHO = $FFEF		;Print Character in Accumulator
RESET = $FF1A		;Bios Getline

;********* Zero Page Variables *********

;For GRAPHIC subroutine
CharacValue = $00	;Character Value
CharacNumb = $01	;Character Number

;For Subroutine
AddressL = $02	        ;Txt Start Address low byte
AddressH = $03	        ;Txt Start Address high byte
PrintLook = $04         ;$00 if a new description is required

;Adventure Values
RoomPos = $10		;Your Position in the game
Room1DoorState = $11    ;Door 1 open = $FF, closed = $00
KeyState = $12          ;Key in Inventory ?
TorchState = $13        ;Torch in inventory ?
DaggerState = $14       ;Dagger in Inventory ?
Room5State = $15        ;LIGHT in Room5 ?
Paralysis = $16         ;Paralysis State

;******** Text Buffers *********

BUF = $0200		;$200-$27F

;********************************************
;************* Main Routine *****************
;********************************************

; *** Initialisation ***

INIT:		CLD		;Clear decimal arithmetic mode
		JSR INTRO	;Introduction Txt
		LDA #$01	
		STA RoomPos	;Init roomPos
                LDA #$00
		STA Room1DoorState	;Door 1 closed
                STA Room5State          ;No Light in Room 5
                STA KeyState            ;No Key in Inventory
                STA TorchState          ;No Torch in Inventory
                STA DaggerState         ;No Dagger in Inventory
                LDA #$FF
                STA Paralysis           ;Paralysis On
		LDA #$00
		STA PrintLook	;Init Description
		JMP MAIN	;Start Game

; *** Main Loop ***

MAIN:		LDA PrintLook	;Need description ?
		BNE MAIN2	;No,branch
		JSR ROOMTXT	;Print Room txt
MAIN2:		JSR ESCAPE	;Get usr input
		JSR ANALYSE	;Analyse input
		JMP MAIN	;Loop

; *** Analyse txt buffer ***

ANALYSE:	;Test General Actions
                testString Help
		jmi ANAHELP	;Yes, branch
		testString Exit
		jmi ANAEXIT	;Yes, branch	
		testString Look
		jmi ANALOOK	;Yes, branch
                testString Shit
		jmi ANASHIT	;Yes, branch
                testString Fuck
		jmi ANASHIT	;Yes, branch
                testString Inventory
                jmi ANAI        ;Yes, branch
                testString Restart
                jmi ANARESTART  ;Yes, branch
		; Now room specific actions
		LDA RoomPos	;Load Room Position
		CMP #$01
		jeq ANA1	;if room 1 go to label
		CMP #$02
		jeq ANA2	;if room 2 go ....
                CMP #$03
		jeq ANA3	;if room 3 go ....
                CMP #$04
		jeq ANA4	;if room 4 go ....
                CMP #$05
		jeq ANA5	;if room 5 go ....
                CMP #$06
		jeq ANA6	;if room 6 go ....
                ;It normally never reach this point
                printString RoomError
                RTS

                ; General Actions Subroutines
ANAHELP:	printString HelpTxt
		RTS
ANAEXIT:	JMP RESET	 ;Jump to monitor
ANARESTART:     JMP INIT         ;Restart the game
ANALOOK:	LDA #$00
		STA PrintLook	;Enable Reprint Description
		RTS
ANASHIT:	printString ShitTxt
ANASHITIT:      JSR ESCAPE	     ;Get usr input
                testString Sorry
		BPL ANASHITIT	        ;No, branch
                printString ShitTxt2
                RTS
ANAI:           printString InventoryTxt
                LDA KeyState
                BMI ANAIKEY
                printString InventoryNothing
                RTS
ANAIKEY:        printString Key
                LDA TorchState
                BPL ANAINOTORCH
                printString Torch
ANAINOTORCH:    LDA DaggerState
                BPL ANAINODAGGER
                printString Dagger
ANAINODAGGER:   RTS
ANANONE:        testString NorthFull
                jmi ANANODIR
                testString North
                BMI ANANODIR
                testString SouthFull
                BMI ANANODIR
                testString South
                BMI ANANODIR
                testString WestFull
                BMI ANANODIR
                testString West
                BMI ANANODIR
                testString EastFull
                BMI ANANODIR
                testString East
                BMI ANANODIR
                testString Up
                BMI ANANODIR
                testString Down
                BMI ANANODIR
                printString DontUnderstand
                RTS
ANANODIR:       printString CantGoThere
                RTS

                ; Room 1 Specific Action
ANA1:		testString SouthFull
		BMI ANA1S
                testString South
		BMI ANA1S
                testString ExamineDoor
                BMI ANA1DOOR
                testString UseKey
                BMI ANA1KEY
                testString OpenDoor
                BMI ANA1KEY
                testString Enter
                BMI ANA1ENTER
                JMP ANANONE
ANA1S:          LDA #$02        ;if South Go to Room 2
                STA RoomPos
                LDA #$00
		STA PrintLook	;Enable Reprint Description
                RTS
ANA1DOOR:       LDA Room1DoorState
                BMI ANA1DOOROPEN
                printString Room1Door1
                RTS
ANA1DOOROPEN:   printString Room1Door2
                RTS
ANA1KEY:        LDA KeyState      ;Key in inventory ?
                BMI ANA1KEYOK     ;Yes, branch
                printString NoKey
                RTS
ANA1KEYOK:      printString UnlockDoor
                LDA #$FF
                STA Room1DoorState  ;Unlock the door
                RTS
ANA1ENTER:      LDA Room1DoorState
                BMI ANA1ENTEROK
                printString Room1Door1
                RTS
ANA1ENTEROK:    LDA #$03        ;if Enter Go to Room 3
                STA RoomPos
                LDA #$00
		STA PrintLook	;Enable Reprint Description
                RTS

                ; Room 2 Specific Action
ANA2:		testString NorthFull
		BMI ANA2N
                testString North
		BMI ANA2N
                testString SouthFull
                BMI ANA2S
                testString South
                BMI ANA2S
                testString ExamineBoat
                BMI ANA2BOAT
                testString ExamineSkeleton
                BMI ANA2SKELETON
                JMP ANANONE
ANA2N:          LDA #$01        ;if North Go to Room 1
                STA RoomPos
                LDA #$00
		STA PrintLook	;Enable Reprint Description
                RTS
ANA2S:          printString Room2Boat
                PLA             ;Destroy JSR ref
                PLA
                JMP RESET
ANA2BOAT:       printString BoatState
                RTS
ANA2SKELETON:   LDA KeyState
                BMI ANA2SKELOLD
                printString Skeleton
                LDA #$FF
                STA KeyState    ;OK, Key in your inventory
                RTS
ANA2SKELOLD:    printString SkeletonNoKey
                RTS

                ; Room 3 Specific Action
ANA3:		testString WestFull
                BMI ANA3W
                testString West
                BMI ANA3W
                testString Up
                BMI ANA3UP
                testString ExamineTorch
                BMI ANA3ETORCH
                testString GetTorch
                BMI ANA3GTORCH
                testString TakeTorch
                BMI ANA3GTORCH
                JMP ANANONE
ANA3W:          LDA #$01        ;if West Go to Room 1
                STA RoomPos
                LDA #$00
		STA PrintLook	;Enable Reprint Description
                RTS
ANA3UP:         LDA #$04        ;if Up Go to Room 4
                STA RoomPos
                LDA #$00
		STA PrintLook	;Enable Reprint Description
                RTS
ANA3ETORCH:     LDA TorchState
                BMI ANA3ENOTCH
                LDA Room5State
                BMI ANA3ENOTCH
                printString ExamineTorchTxt
                RTS
ANA3ENOTCH:     printString TorchGone
                RTS
ANA3GTORCH:     LDA TorchState
                BMI ANA3GOTHAVE
                printString GetTorchTxt
                LDA #$FF
                STA TorchState
                RTS
ANA3GOTHAVE:    printString AlreadyHave
                RTS

                ; Room 4 Specific Action
ANA4:		testString Up
                BMI ANA4UP
                testString Down
                BMI ANA4DOWN
                testString SouthFull
                BMI ANA4S
                testString South
                BMI ANA4S
                testString ExaminePicture
                BMI ANA4EPIC
                testString ExamineBed
                BMI ANA4EBED
                JMP ANANONE
ANA4UP:         LDA #$06        ;if Up Go to Room 6
                STA RoomPos
                LDA #$00
		STA PrintLook	;Enable Reprint Description
                RTS
ANA4DOWN:       LDA #$03        ;if Down Go to Room 3
                STA RoomPos
                LDA #$00
		STA PrintLook	;Enable Reprint Description
                RTS
ANA4S:          LDA #$05        ;if South Go to Room 5
                STA RoomPos
                LDA #$00
		STA PrintLook	;Enable Reprint Description
                RTS
ANA4EPIC:       printString ExaminePictureTxt
                RTS
ANA4EBED:       printString ExamineBedTxt
                RTS

                ; Room 5 Specific Action
ANA5:		testString NorthFull
                BMI ANA5N
                testString North
                BMI ANA5N
                testString UseTorch
                BMI ANA5LIGHT
                LDA Room5State          ;Is there light ?
                BPL ANA5DARK            ;No, block examine actions
                testString ExamineDesk
                BMI ANA5DESK
                testString ExamineBook
                BMI ANA5BOOK
                JMP ANANONE
ANA5DARK:       printString TooDark
                RTS
ANA5DESK:       LDA DaggerState
                BMI ANA5DESKMT
                printString ExamineDeskTxt
                LDA #$FF
                STA DaggerState         ;Put dagger in bag
                RTS
ANA5DESKMT:     printString DeskEmpty
                RTS
ANA5BOOK:       printString ExamineBookTxt
                RTS
ANA5N:          LDA #$04        ;if North Go to Room 4
                STA RoomPos
                LDA #$00
		STA PrintLook	;Enable Reprint Description
                RTS
ANA5LIGHT:      LDA Room5State          ;Already lit ?
                BMI ANA5LITYET          ;Yes, branch
                LDA TorchState          ;Torch in inventory ?
                BMI ANA5LIGHTOK         ;Yes, branch
                printString NoTorch
                RTS
ANA5LITYET:     printString AlreadyLit
                RTS
ANA5LIGHTOK:    LDA #$FF
                STA Room5State  ;LIGHT in room 5
                LDA #$00
		STA PrintLook	;Enable Reprint Description
                STA TorchState  ;No Torch in Inventory
                RTS

                ; Room 6 Specific Action
ANA6:		testString Down
                BMI ANA6D
                testString SayAnaetosh
                BMI ANA6PARA
                testString UseDagger
                BMI ANA6DAGGER
                testString Kill
                BMI ANA6DAGGER
                testString Attack
                BMI ANA6DAGGER
                JMP ANANONE
ANA6D:          LDA Paralysis
                BPL ANA6DOK
                printString Death
                PLA             ;Destroy JSR ref
                PLA
                JMP RESET
ANA6DOK:        LDA #$04        ;if Down Go to Room 4
                STA RoomPos
                LDA #$FF
                STA Paralysis   ;Paralysis On
                LDA #$00
		STA PrintLook	;Enable Reprint Description
                RTS
ANA6PARA:       printString SayAnaetoshTxt
                LDA #$00
                STA Paralysis   ;No Paralysis
                RTS
ANA6DAGGER:     LDA DaggerState       ;Dagger in inventory ?
                BMI ANA6DAGCHK        ;Yes, check paralysis
                printString NoDagger
                RTS
ANA6DAGCHK:     LDA Paralysis
                BPL ANA6DAGGEROK
                printString Cantmove
                RTS
ANA6DAGGEROK:   printString Win
                printString WinArt
                PLA             ;Destroy JSR ref
                PLA
                JMP RESET

; *** Print Room Information ***

ROOMTXT:	LDA #$FF	;Mask for
		STA PrintLook	;No more descrip

		;Test for each Room
		LDA RoomPos	;Load Room Position
		CMP #$01
		jeq ROOMTXT1	;if room 1 go to label
		CMP #$02
		jeq ROOMTXT2	;if room 2 go ....
                CMP #$03
		jeq ROOMTXT3	;if room 3 go ....
                CMP #$04
		jeq ROOMTXT4	;if room 4 go ....
                CMP #$05
		jeq ROOMTXT5	;if room 5 go ....
                CMP #$06
		jeq ROOMTXT6	;if room 6 go ....
		;It normally never reach this point
                printString RoomError
                RTS

ROOMTXT1:	printRoomInfo Room1Txt,Room1Obj,Room1Exit
		RTS
ROOMTXT2:	printRoomInfo Room2Txt,Room2Obj,Room2Exit
		RTS
ROOMTXT3:	printRoomInfo Room3Txt,Room3Obj,Room3Exit
		RTS
ROOMTXT4:	printRoomInfo Room4Txt,Room4Obj,Room4Exit
		RTS
ROOMTXT5:	LDA Room5State
                BMI ROOMTXT5LIGHT
                printString Room5TxtNoLight
		RTS
ROOMTXT5LIGHT:  printRoomInfo Room5Txt,Room5Obj,Room5Exit
                RTS
ROOMTXT6:	printRoomInfo Room6Txt,Room6Obj,Room6Exit
                printString Room6Art
		RTS

;********************************************
;*************** SubRoutines ****************
;********************************************


;*** Compare user BUF and txt address ***
;param BUF user buffer starting address
;param AddressL, AddressH from a txt address
;EOF charactere for txt is % = 0x25
;return Accumulator with 0x00 if false, 0xFF if true

COMPARE:	LDY #$FF	;reset index
COMPIT:		INY
		LDA (AddressL),Y;load charac
		CMP #$25	;EOF ?
		BEQ COMPCHK	;Yes, check word boundary
		ORA #$80	;Set Bit7 to compare with kbd output
		CMP BUF,Y	;Compare with user buffer
		BEQ COMPIT	;Ok,continue comparison
		LDA #$00	;Result false
		RTS
COMPCHK:	LDA BUF,Y	;Next char in user buffer
		CMP #$8D	;CR ? (end of input)
		BEQ COMPTRUE
		CMP #$A0	;Space ? (word separator)
		BEQ COMPTRUE
		LDA #$00	;Partial word match, false
		RTS
COMPTRUE:	LDA #$FF	;Result true
		RTS


;*** Get user input in txt buffer Subroutine ***
;param BUF Txt buffer starting address

NOTCR: 		CMP #$DF	;Backspace ?
		BEQ BACKSPACE
		CMP #$9B	;Escape ?
		BEQ ESCAPE
		INY
		BPL NEXTCHAR	;Auto ESC if Y > 127
ESCAPE:		LDA #$8D	;new line
		JSR ECHO
		LDA #$3E	;">"
		JSR ECHO
		LDY #$01	;Init txt index
BACKSPACE:	DEY
		BMI ESCAPE	;More than 127 char, reset
NEXTCHAR:	LDA KBDCR
		BPL NEXTCHAR	;Key Ready ?
		LDA KBD		;Load charac
		STA BUF,Y	;Add to txt buffer
		JSR ECHO
		CMP #$8D	;CR ?
		BNE NOTCR	;No
		RTS

;*** Subroutine for repetitive GRAPH ***
;param CharacValue, CharacNumb

GRAPH: 		LDY #$00	;reset index
		LDA CharacValue	;Load charac Value
GRAPHIT:	INY		;Advance index
		JSR ECHO	;Output Charac
		CPY CharacNumb	;Number reach ?
		BNE GRAPHIT	;No, continue
		RTS		;Return

;*** Subroutine for reading Txt adress ***
;param AddressL, AdressH
;Txt Format : EOF = % = 0x25, CR = & = 0x26

READTXT:	LDY #$FF	;reset index
READTXTIT:	INY
		LDA (AddressL),Y;Absolute Adress indirect read
		CMP #$25	;"%" ? End of message ?
		BEQ READTXTEND	;Yes Branch
		CMP #$26	;"&" ? New line ?
		BEQ READTXTCR	;Yes, branch
		JSR ECHO
		JMP READTXTIT	;Continue
READTXTCR:	LDA #$8D	;CR
		JSR ECHO
		JMP READTXTIT	;Continue
READTXTEND:	RTS

;*** Draw introduction Subroutine ***

INTRO: 		LDA #$8D	;CR
		JSR ECHO
		JSR ECHO
		LDA #$08	;8 space
		STA CharacNumb	;Store it
		LDA #$A0	;" "
		STA CharacValue	;Store Space
		JSR GRAPH	;Output 8 space
		printString CopyMess1
		LDA #$0A	;10 space
		STA CharacNumb	;Store it
		JSR GRAPH	;Output 10 space
		printString CopyMess2
		LDA #$0C	;12 space
		STA CharacNumb	;Store it
		JSR GRAPH	;Output 12 space
		printString CopyMess3

		;Draw the ASCII Tower Line 1
		LDA #$8D	;CR
		JSR ECHO
		LDA #$10	;16 space
		STA CharacNumb	;Store it
		LDA #$A0	;" "
		STA CharacValue	;Store Space
		JSR GRAPH	;Output 16 space
		LDA #$08
		STA CharacNumb
		LDA #$C9	;"I"
		STA CharacValue
		JSR GRAPH
		;Line 2
		LDA #$8D	;CR
		JSR ECHO
		LDA #$10	;16 space
		STA CharacNumb	;Store it
		LDA #$A0	;" "
		STA CharacValue	;Store Space
		JSR GRAPH	;Output 16 space
		LDA #$C9	;"I"
		JSR ECHO
		LDA #$06	;6 space now
		STA CharacNumb
		JSR GRAPH
		LDA #$C9	;"I"
		JSR ECHO
		;Line 3
		LDA #$8D	;CR
		JSR ECHO
		LDA #$10	;16 space
		STA CharacNumb	;Store it
		LDA #$A0	;" "
		STA CharacValue	;Store Space
		JSR GRAPH	;Output 16 space
		LDA #$C9	;"I"
		JSR ECHO
		LDA #$06	;6 space now
		STA CharacNumb
		JSR GRAPH
		LDA #$C9	;"I"
		JSR ECHO
		;Line 4
		LDA #$8D	;CR
		JSR ECHO
		LDA #$10	;16 space
		STA CharacNumb	;Store it
		LDA #$A0	;" "
		STA CharacValue	;Store Space
		JSR GRAPH	;Output 16 space
		LDA #$C9	;"I"
		JSR ECHO
		LDA #$06	;6 space now
		STA CharacNumb
		JSR GRAPH
		LDA #$C9	;"I"
		JSR ECHO
		;Line 5
		LDA #$8D	;CR
		JSR ECHO
		LDA #$10	;16 space
		STA CharacNumb	;Store it
		LDA #$A0	;" "
		STA CharacValue	;Store Space
		JSR GRAPH	;Output 16 space
		LDA #$C9	;"I"
		JSR ECHO
		LDA #$02	;2 space now
		STA CharacNumb
		JSR GRAPH
		LDA #$C9	;"I"
		STA CharacValue
		JSR GRAPH
		LDA #$A0	;" "
		STA CharacValue
		JSR GRAPH
		LDA #$C9
		JSR ECHO
		;Line 6
		LDA #$8D	;CR
		JSR ECHO
		LDA #$10	;16 space
		STA CharacNumb	;Store it
		LDA #$A0	;" "
		STA CharacValue	;Store Space
		JSR GRAPH	;Output 16 space
		LDA #$C9	;"I"
		JSR ECHO
		LDA #$02	;2 space now
		STA CharacNumb
		JSR GRAPH
		LDA #$C9	;"I"
		STA CharacValue
		JSR GRAPH
		LDA #$A0	;" "
		STA CharacValue
		JSR GRAPH
		LDA #$C9
		JSR ECHO
		;Line 7
		LDA #$8D	;CR
		JSR ECHO
		LDA #$0C	;12 space
		STA CharacNumb	;Store it
		LDA #$A0	;" "
		STA CharacValue	;Store Space
		JSR GRAPH	;Output 12 space
		LDA #$C9	;"I"
		STA CharacValue
		LDA #$10	;16 "I"
		STA CharacNumb
		JSR GRAPH
		; Choice Menu
		LDA #$8D	;CR
		JSR ECHO
		JSR ECHO
		LDA #$0C	;12 space
		STA CharacNumb	;Store it
		LDA #$A0	;" "
		STA CharacValue	;Store Space
		JSR GRAPH	;Output 12 space
		printString IntroChoice

		;Check Intro Choice
		LDA #$3E	;">"
		JSR ECHO
INTNXTCHAR:	LDA KBDCR
		BPL INTNXTCHAR	;Key Ready ?
		LDA KBD		;Load charac
		CMP #$B1	;"1" PLAY
		BEQ INTEND	;return
		CMP #$B2	;"2" HELP
		BEQ INTHELP	;Yes,branch
		JMP INTNXTCHAR
INTHELP:	printString HelpTxt
INTEND:		printString LetBegin
		RTS	

;********************************************
;************** DATA ADDRESS ****************
;********************************************

;Data Txt Format : EOF = % = 0x25, CR = & = 0x26
;Beware Data must be under 0xFF byte long

;*** Copyrights

CopyMess1: 	.byte "LITTLE TOWER V1.0&%"
CopyMess2:	.byte "APPLE 1 COOL GAME&%"
CopyMess3:	.byte "WRITTEN BY A.VERHILLE&%"

;*** Introduction choice

IntroChoice:	.byte "1] PLAY  2] HELP&&%"

;*** Misc Txt

LetBegin:	.byte "OK, NOW LET'S BEGIN ...&&%"
HelpTxt: 	.byte "THIS IS A BASIC TXT ADVENTURE.&VALIDS COMMANDS ARE:&&DIRECTION: N,S,W,UP,DOWN.&ACTION: LOOK,EXAMINE,ENTER,&GET,TAKE,USE,SAY,OPEN.&SPECIAL: INVENTORY,HELP,&RESTART,EXIT.&OBJECT: BOAT,DOOR,AND MORE...&&COMPLEX COMMAND USE TWO WORD MAX&%"
ObjTxt:		.byte "OBJECT(S): %"
ExitTxt:	.byte "EXIT(S): %"
ShitTxt:	.byte "HEY, BECAUSE YOU SAY THAT,&YOU MUST TYPE 'SORRY' TO CONTINUE.&OBEY CAN BE A GOOD IDEA&%"
ShitTxt2:       .byte "OK, YOU'RE A GOOD BOY (OR GIRL),&YOU CAN PLAY NOW ...&%"
RoomError:      .byte "ROOM POSITION ERROR :-(((((&%"
InventoryTxt:   .byte "YOUR BAG CONTAINS :&%"
Key:            .byte "A KEY&%"
Torch:          .byte "A TORCH&%"
Dagger:         .byte "A SILVER DAGGER&%"
InventoryNothing:   .byte "NOTHING&%"

;*** Action Commands
;Generals Commands
Exit:		.byte "EXIT%"
Help:		.byte "HELP%"
Look:		.byte "LOOK%"
Shit:		.byte "SHIT%"
Fuck:           .byte "FUCK%"
Sorry:          .byte "SORRY%" 
Inventory:      .byte "INVENTORY%"
Restart:        .byte "RESTART%"

; Directions Commands
North:          .byte "N%"
NorthFull:      .byte "NORTH%"
South:          .byte "S%"
SouthFull:      .byte "SOUTH%"
West:           .byte "W%"
WestFull:       .byte "WEST%"
East:           .byte "E%"
EastFull:       .byte "EAST%"
Up:             .byte "UP%"
Down:           .byte "DOWN%"

; Actions Commands
SayAnaetosh:    .byte "SAY ANAETOSH%"
UseDagger:      .byte "USE DAGGER%"
UseTorch:       .byte "USE TORCH%"
ExamineDesk:    .byte "EXAMINE DESK%"
ExamineBook:    .byte "EXAMINE BOOK%"
ExaminePicture: .byte "EXAMINE PICTURE%"
ExamineBed:     .byte "EXAMINE BED%"
ExamineTorch:   .byte "EXAMINE TORCH%"
ExamineBoat:    .byte "EXAMINE BOAT%"
ExamineSkeleton:.byte "EXAMINE SKELETON%"
ExamineDoor:	.byte "EXAMINE DOOR%"
GetTorch:       .byte "GET TORCH%"
TakeTorch:      .byte "TAKE TORCH%"
Enter:          .byte "ENTER%"
UseKey:         .byte "USE KEY%"
OpenDoor:       .byte "OPEN DOOR%"
Kill:           .byte "KILL%"
Attack:         .byte "ATTACK%"

;*** Room Information

Room1Txt:	.byte "YOU'RE IN A DARK FOREST.&ELUSIVE SHADOWS ARE FLYING AROUND.&IN FRONT OF YOU, A THREE FLOOR TOWER&SEEMS TO BE WELCOMING.&TO THE SOUTH, A LAKE DISAPPEARING&INTO THE FOG&%"
Room1Obj:	.byte "DOOR&%"
Room1Exit:	.byte "S,ENTER&%"

Room2Txt:	.byte "YOU'RE ON THE LAKE'S BANK,&THERE IS A SKELETON HERE, AND AN OLD&SMALL BOAT FLOATTING ON THE WATER. IT&CAN PROBABLY TRANSPORT YOU TO THE SOUTH&%"
Room2Obj:	.byte "SKELETON,BOAT&%"
Room2Exit:	.byte "N,S&%"

Room3Txt:       .byte "YOU'RE IN A WORKSHOP. LOTS OF WOOD'S&PLANKS ARE RESTING ON THE WALLS. THERE&ARE SOME TORCHS ON A WORKBENCH, STAIRS&GOING UP AND A DOOR TO THE WEST.&%"
Room3Obj:       .byte "TORCH&%"
Room3Exit:      .byte "W,UP&%"

Room4Txt:       .byte "WELCOME TO THE TOWER'S BEDROOM.&THERE ARE SOME PICTURES ON THE WALL,&BUT NOTHING MORE SEEMS USEFUL HERE.&OH, THERE'S A DOOR ON THE SOUTH TOO.&%"
Room4Obj:       .byte "PICTURE,BED&%"
Room4Exit:      .byte "UP,DOWN,S&%"

Room5Txt:       .byte "IN THIS ROOM, THERE'S A LIBRARY WHERE&HUNDRED OF BOOKS ARE STORED.&A WOODEN DESK AND A CHAIR&ARE RESTING HERE.&%"
Room5Obj:       .byte "DESK,BOOK&%"
Room5Exit:      .byte "N&%"

Room6Txt:       .byte "THIS ROOM IS TOTALY WIPED OUT,&YOU CAN SEE THE STARS FROM HERE.&IN THE MIDDLE OF THE MESS,&A DARK MAN IS LOOKING AT YOU.&%"
Room6Obj:       .byte "NOTHING&%"
Room6Exit:      .byte "DOWN&%"

;*** Room Specific Answers

Room1Door1:     .byte "THE DOOR IS CLOSED&%"
Room1Door2:     .byte "THE DOOR IS UNLOCKED AND OPEN,&TYPE 'ENTER' NOW&%"
Room2Boat:      .byte "THE BOAT IS SINKING !!!!&THAT'S THE END.&%"
BoatState:      .byte "THERE IS A LITTLE HOLE IN THE BOAT,&GOOD TO KNOW !&%"
Skeleton:       .byte "THERE IS A KEY UNDER THE SKELETON&YOU GET THE KEY WITH YOU.&%"
NoKey:          .byte "NO KEY IN YOUR INVENTORY !!!!&%"
UnlockDoor:     .byte "OK, THE DOOR IS UNLOCKED&%"
ExamineTorchTxt:.byte "GOOD TORCH FOR GOOD LIGHT !&%"
GetTorchTxt:    .byte "OK, THE TORCH IS IN YOUR BAG.&%"
ExaminePictureTxt: .byte "THERE ARE SOME CABALISTICS SIGNS.&ONE ONLY CAN BE READ :&'ANAETOSH' IS THE WORD WHO CAN SAVE&YOU FROM PARALYSIS.&%"
ExamineBedTxt:  .byte "THERE'S NOTHING ON THE BED&AND UNDER THE BED.&%"
ExamineDeskTxt: .byte "THERE IS A SILVER DAGGER HERE&YOU GET THE DAGGER WITH YOU.&%"
ExamineBookTxt: .byte "LOTS OF BOOK TALK ABOUT&VAMPIRES EXTERMINATION.&%"
Room5TxtNoLight:.byte "THERE IS NO LIGHT HERE,&APART FROM THE NORTH DOOR.&%"
SayAnaetoshTxt: .byte "YEAHH, IT WORKS !!!&YOU CAN MOVE NOW.&%"
Cantmove:       .byte "IT'S IMPOSSIBLE, YOU CAN'T MOVE !&%"
NoDagger:       .byte "YOU DON'T HAVE A DAGGER !&%"
NoTorch:        .byte "YOU DON'T HAVE A TORCH !&%"
TooDark:        .byte "IT'S TOO DARK TO SEE ANYTHING !&%"
SkeletonNoKey:  .byte "JUST AN OLD SKELETON&%"
AlreadyHave:    .byte "YOU ALREADY HAVE THAT&%"
DeskEmpty:      .byte "THERE IS NOTHING ON THE DESK&%"
DontUnderstand: .byte "I DON'T UNDERSTAND, TYPE 'HELP'&%"
CantGoThere:    .byte "YOU CAN'T GO THAT WAY&%"
TorchGone:      .byte "YOU ALREADY TOOK THE BEST ONE&%"
AlreadyLit:     .byte "THE ROOM IS ALREADY LIT&%"
Win:            .byte "THE VAMPIRE IS DISAPPEARING&UNDER YOUR ATTACK !&%"
Death:          .byte "YOU ARE TRYING TO ESCAPE,&BUT YOU CAN'T MOVE. THE VAMPIRE&IS SUCKING YOUR BLOOD NOW.&THAT'S THE END.&%"

;*** ASCII Art

Room6Art:       .byte "&&      () ()&       \/&   ___/  \___&  /\/\/\/\/\/\&  \_/      \_/&&%"
WinArt:         .byte "&&    *******************&    *                 *&    * CONGRATULATIONS *&    *   YOU WIN !!    *&    *                 *&    *******************&&%"
