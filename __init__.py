def play(hands,process,firstCardOnLeft=True,verbose=False,fast=True):
    a,b = hands #hands are called a and b
    if not fast: 
		print process,"-","Mani iniziali: ",hands  
    if not firstCardOnLeft:
        a.reverse()
        b.reverse()
    stack = [] #cards in the middle
    turns = 0
    tricks = 0
    player = 1 #alternates between 1 and -1
    while len(a)!=0 and len(b)!=0: #game terminates when a or b's hands are empty
        battle_in_progress = False
        cards_to_play = 1
        while cards_to_play>0: #current player may play up to cards_to_play cards
            turns=turns+1
            
            try:
                if player==1:
                    #grab next card from first character of string a
                    next_card = a[0] 
                    a = a[1:]
                else:
                    #grab next card from first character of string b
                    next_card = b[0]
                    b = b[1:]
            except IndexError:
                break #ran out of cards to play, game over...
            
            stack.append(next_card) #add to the stack
            
            if next_card==0:
                #not a court card
                if battle_in_progress:
                    cards_to_play=cards_to_play-1 #this player needs keep trying to find a court card
                else:
                    player = player*-1 #no court cards found yet, back to other player
            else:
                #court card found, back to the other player
                battle_in_progress = True 
                cards_to_play = next_card
                player = player*-1
                
        #end of trick, make the losing player pick up the cards in the stack
        tricks = tricks+1
        if player==1:
            b.extend(stack) 
            stack = []
        else:
            a.extend(stack) 
            stack = []
        
        
        player = player*-1
        
        if(tricks>100):
			print process, "-","Forse infinita:", hands
			return
			
        
        #print current status
        if verbose:
            print process,"-","Situazione:",a,b
            
    if not fast: 
		print "%s - Ci sono stati %d turni e %d prese" % (process,turns,tricks)
