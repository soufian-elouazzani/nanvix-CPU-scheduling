/*
 * Copyright(C) 2011-2016 Pedro H. Penna   <pedrohenriquepenna@gmail.com>
 *              2015-2016 Davidson Francis <davidsondfgl@hotmail.com>
 *
 * This file is part of Nanvix.
 *
 * Nanvix is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Nanvix is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Nanvix. If not, see <http://www.gnu.org/licenses/>.
 */
#include <nanvix/klib.h> 
#include <nanvix/clock.h>
#include <nanvix/const.h>
#include <nanvix/hal.h>
#include <nanvix/pm.h>
#include <signal.h>

/* Paramètres du MLFQ CTSS */
#define MLFQ_LEVELS      8     /* 8 niveaux (0=plus haut, 7=plus bas) */
#define QUANTUM_BASE    10     /* Quantum de base pour niveau 0 */
#define MAX_MLFQ_LEVEL  7      /* PRIO_USER + 7 */
#define BOOST_INTERVAL  1000   /* Période de remontée globale */

/**
 * @brief Schedules a process to execution.
 *
 * @param proc Process to be scheduled.
 */
PUBLIC void sched(struct process *proc)
{
    proc->state = PROC_READY;
}

/**
 * @brief Stops the current running process.
 */
PUBLIC void stop(void)
{
    curr_proc->state = PROC_STOPPED;
    sndsig(curr_proc->father, SIGCHLD);
    yield();
}

/**
 * @brief Resumes a process.
 *
 * @param proc Process to be resumed.
 *
 * @note The process must stopped to be resumed.
 */
PUBLIC void resume(struct process *proc)
{
    /* Resume only if process has stopped. */
    if (proc->state == PROC_STOPPED)
    {
        /* Wake-up Boost
         * Un processus qui se réveille (ex: retour de wait, fin d'I/O)
         * doit être traité comme interactif et remonter au sommet.
         */
        if (proc->priority >= PRIO_USER)
        {
            proc->priority = PRIO_USER; /* Remonte au niveau 0 */
            proc->counter = 0;          /* Réinitialise son temps d'attente */
        }
        
        sched(proc);
    }
}

/**
 * @brief Promouvoit un processus au niveau le plus élevé du MLFQ
 *        (pour la touche Enter du CTSS)
 * 
 * @param proc Processus à promouvoir
 */
PUBLIC void mlfq_promote(struct process *proc)
{
    if (proc->priority >= PRIO_USER) {
        proc->priority = PRIO_USER; /* Niveau le plus élevé */
    }
}

/**
 * @brief Yields the processor.
 */
PUBLIC void yield(void)
{
    struct process *p;
    struct process *next;
    static unsigned int ticks_since_boost = 0;
    
    /* ========================================================= */
    /* PHASE 1: Mise à jour MLFQ du processus courant            */
    /* ========================================================= */
    
    if (curr_proc->state == PROC_RUNNING)
    {
        /* Appliquer MLFQ seulement aux processus utilisateur */
        if (curr_proc->priority >= PRIO_USER)
        {
            /* 
             * Logique CTSS:
             * - CPU-bound (a utilisé tout son quantum): descend d'un niveau
             * - I/O-bound (a cédé volontairement): remonte au niveau 0
             */
            if (curr_proc->counter <= 0) /* CPU-bound */
            {
                /* Descendre d'un niveau si pas déjà au plus bas */
                if (curr_proc->priority < PRIO_USER + MAX_MLFQ_LEVEL)
                {
                    curr_proc->priority++;
                }
            }
            else /* I/O-bound */
            {
                /* Remonter au niveau le plus élevé */
                curr_proc->priority = PRIO_USER;
            }
        }
        
        /* Réinitialiser le counter avant de mettre en READY
         * Le counter doit passer de "quantum restant" à "temps d'attente"
         * On le met à 0 pour que le processus ait un temps d'attente initial de 0
         */
        curr_proc->counter = 0;
        
        /* Mettre en READY pour rescheduling */
        sched(curr_proc);
    }

    /* ========================================================= */
    /* PHASE 2: Gestion des alarmes ET Priority Boost            */
    /* ========================================================= */
    ticks_since_boost++;
    
    /* Priority Boost global tous les BOOST_INTERVAL ticks */
    if (ticks_since_boost >= BOOST_INTERVAL)
    {
        for (p = FIRST_PROC; p <= LAST_PROC; p++)
        {
            if (!IS_VALID(p))
                continue;
                
            if (p->priority >= PRIO_USER)
            {
                p->priority = PRIO_USER; /* Remonter tous au niveau 0 */
                p->counter = 0;          /* Réinitialiser le temps d'attente */
            }
        }
        ticks_since_boost = 0;
    }
    
    /* Gestion des alarmes */
    for (p = FIRST_PROC; p <= LAST_PROC; p++)
    {
        if (!IS_VALID(p))
            continue;

        if ((p->alarm) && (p->alarm < ticks))
            p->alarm = 0, sndsig(p, SIGALRM);
    }

    /* ========================================================= */
    /* PHASE 3: Sélection CTSS - Par ordre de priorité           */
    /* ========================================================= */
    
    /* Initialiser avec IDLE comme fallback */
    next = IDLE;
    
    /* Étape 1: Chercher un processus système READY */
    for (p = FIRST_PROC; p <= LAST_PROC; p++)
    {
        if (!IS_VALID(p))
            continue;
            
        if (p->state != PROC_READY)
            continue;
            
        /* Processus système (priorité < PRIO_USER) */
        if (p->priority < PRIO_USER)
        {
            /* Sélectionner le premier processus système trouvé */
            next = p;
            break; /* Les processus système sont toujours prioritaires */
        }
    }
    
    /* Étape 2: Si aucun processus système, chercher dans MLFQ */
    if (next == IDLE)
    {
        /* Parcourir les niveaux MLFQ du plus haut au plus bas */
        for (int level = 0; level <= MAX_MLFQ_LEVEL; level++)
        {
            int target_priority = PRIO_USER + level;
            struct process *candidate = NULL;
            int max_wait_time = -1;
            
            /* Chercher dans ce niveau */
            for (p = FIRST_PROC; p <= LAST_PROC; p++)
            {
                if (!IS_VALID(p))
                    continue;
                    
                if (p->state != PROC_READY)
                    continue;
                    
                if (p->priority == target_priority)
                {
                    /* À l'intérieur d'un même niveau, on prend celui qui attend le plus */
                    if (p->counter > max_wait_time)
                    {
                        max_wait_time = p->counter;
                        candidate = p;
                    }
                }
            }
            
            /* Si on a trouvé un candidat dans ce niveau, c'est le gagnant */
            if (candidate != NULL)
            {
                next = candidate;
                break; /* CTSS s'arrête au premier niveau non vide */
            }
        }
    }

    /* ========================================================= */
    /* PHASE 4: Mise à jour des temps d'attente (aging)          */
    /* ========================================================= */
    
    /* Incrémenter le temps d'attente de tous les processus READY */
    for (p = FIRST_PROC; p <= LAST_PROC; p++)
    {
        if (!IS_VALID(p))
            continue;
            
        if (p->state == PROC_READY)
        {
            p->counter++; /* counter = temps d'attente */
        }
    }

    /* ========================================================= */
    /* PHASE 5: Attribution du quantum selon CTSS                */
    /* ========================================================= */
    
    /* Réinitialiser l'état du processus sélectionné */
    next->state = PROC_RUNNING;
    /* On ne réinitialise PAS counter ici - il devient le quantum restant */
    
    /* Calculer le quantum selon le type de processus */
    if (next->priority < PRIO_USER)
    {
        /* Processus système: quantum fixe */
        next->counter = PROC_QUANTUM; /* counter devient le quantum restant */
    }
    else if (next == IDLE)
    {
        /* IDLE: quantum très court */
        next->counter = 1;
    }
    else
    {
        /* Processus utilisateur: quantum exponentiel selon niveau MLFQ */
        int mlfq_level = next->priority - PRIO_USER;
        
        /* S'assurer que le niveau est dans les bornes */
        if (mlfq_level < 0) mlfq_level = 0;
        if (mlfq_level > MAX_MLFQ_LEVEL) mlfq_level = MAX_MLFQ_LEVEL;
        
        /* Quantum = QUANTUM_BASE * (2^niveau) comme dans CTSS */
        next->counter = QUANTUM_BASE * (1 << mlfq_level);
    }

    /* ========================================================= */
    /* PHASE 6: Changement de contexte                           */
    /* ========================================================= */
    
    if (curr_proc != next)
        switch_to(next);
}
